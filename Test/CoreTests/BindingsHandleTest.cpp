// 句柄表（`csharp-bindings-generator` 任务 2.4）：序号 + 代际、配对释放、失效报错、参数侧反查。
//
// 四条读数：
//   ① 实现返回智能指针 → 拿到非零句柄；
//   ② 有效句柄释放一次 → 成功、错误码保持 0（证明反查命中）；
//   ③ 同一个句柄再释放 → 错误码 1001（句柄已失效），进程不崩；
//   ④ 释放后再反查仍然是失效（旧句柄不会因为"序号复用"而突然生效）。

#include "BindableShapes.h"

#include "ca_bind_fixture__IBindRow09.Bindings.cpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace
{
	class FakeRow09 final : public ca_bind_fixture::IBindRow09
	{
	public:
		castl::shared_ptr<ca_bind_fixture::BindScalarThing> AcquireThing() override
		{
			if (returnNull)
			{
				return nullptr;   // 任务 5.8：1002（实现返回空指针）的触发器
			}
			auto thing = castl::make_shared<ca_bind_fixture::BindScalarThing>();
			thing->id = 7u;
			thing->weight = 1.5f;
			return thing;
		}

		// 第二条读数要用：让实现返回空指针时拿到 0 句柄 + 错误码 1002
		bool returnNull = false;
	};
}

bool RunBindingsHandleTest()
{
	FakeRow09 object;
	const uint64_t handle = ca_bind_fixture__IBindRow09__AcquireThing(&object);
	if (handle == 0u)
	{
		std::puts("  AcquireThing 没有返回句柄");
		return false;
	}

	// ①' 任务 1.1：句柄表现在是**一张**表，表项带 `kind` 区分强/弱两类。
	// 合并之前产物里连 `IBindRow09HandleEntry` 的这个字段都不存在，所以这条断言本身就是红态。
	{
		const uint64_t indexMask = (1ull << 48) - 1ull;
		const std::size_t slot = static_cast<std::size_t>((handle & indexMask) - 1ull);
		if (slot >= ca_bind_fixture__IBindRow09_handles.size() || ca_bind_fixture__IBindRow09_handles[slot].kind != 1u)
		{
			std::puts("  强句柄的表项 kind 应为 1（同一张表里区分强/弱）");
			return false;
		}
		// 再登记一个弱句柄：断言两类能共存于同一张表（不抬高引用计数由 1.4 单独验）
		std::shared_ptr<void> keep(&object, [](void*) {});
		const uint64_t weakHandle = ca_bind_fixture__IBindRow09_registerWeakHandle(&object, std::weak_ptr<void>(keep), "");
		const std::size_t weakSlot = static_cast<std::size_t>((weakHandle & indexMask) - 1ull);
		if (weakHandle == 0u || weakSlot >= ca_bind_fixture__IBindRow09_handles.size() ||
			ca_bind_fixture__IBindRow09_handles[weakSlot].kind != 2u)
		{
			std::puts("  弱句柄没有登记进同一张表（kind 应为 2）");
			return false;
		}
		ca_bind_fixture__IBindRow09_releaseHandle(&object, weakHandle);
	}

	// ② 有效句柄释放一次：成功且错误码回到 0
	ca_bind_fixture__IBindRow09__ReleaseAcquireThing(&object, handle);
	if (ca_bind_fixture__IBindRow09__LastErrorCode(&object) != 0)
	{
		std::printf("  有效句柄释放后错误码应为 0，实际 %d\n", ca_bind_fixture__IBindRow09__LastErrorCode(&object));
		return false;
	}

	// ②' 任务 1.2：释放必须**放开强引用** —— 否则对象永不析构，`WindowSystem` 那条
	// 回收条件 `use_count() == 1 && WindowShouldClose()` 永远不成立（窗口永不回收）。
	// 判据直接读表项，不用 `use_count()` 差值（差值会被"删得不干净"骗过）。
	{
		const uint64_t indexMask = (1ull << 48) - 1ull;
		const std::size_t slot = static_cast<std::size_t>((handle & indexMask) - 1ull);
		if (slot < ca_bind_fixture__IBindRow09_handles.size() && ca_bind_fixture__IBindRow09_handles[slot].strong)
		{
			std::puts("  释放句柄后生成物仍持有强引用 —— 对象永远不会被析构");
			return false;
		}
	}

	// ③ 同一句柄再释放：失效错误码 1001
	ca_bind_fixture__IBindRow09__ReleaseAcquireThing(&object, handle);
	if (ca_bind_fixture__IBindRow09__LastErrorCode(&object) != 1001)
	{
		std::printf("  重复释放应报 1001，实际 %d\n", ca_bind_fixture__IBindRow09__LastErrorCode(&object));
		return false;
	}
	// ③' 任务 1.3：泛用解析入口对已失效的句柄必须返回空 + 1001
	// （它是 C# 侧"用句柄调方法"的唯一通路：所有方法入口的第一个参数都是 `void* self`）
	if (ca_bind_fixture__IBindRow09__ResolveHandle(&object, handle) != nullptr)
	{
		std::puts("  ResolveHandle 对已失效的句柄仍然返回了指针");
		return false;
	}
	if (ca_bind_fixture__IBindRow09__LastErrorCode(&object) != 1001)
	{
		std::printf("  ResolveHandle 失败后错误码应为 1001，实际 %d\n", ca_bind_fixture__IBindRow09__LastErrorCode(&object));
		return false;
	}

	char text[64] = { 0 };
	ca_bind_fixture__IBindRow09__LastErrorText(&object, text, sizeof(text));
	if (std::strstr(text, "stale") == nullptr)
	{
		std::printf("  失效后文本里没有说明：%s\n", text);
		return false;
	}

	// ④ 再释放一次仍然是 1001（旧句柄不会复活）
	ca_bind_fixture__IBindRow09__ReleaseAcquireThing(&object, handle);
	if (ca_bind_fixture__IBindRow09__LastErrorCode(&object) != 1001)
	{
		std::puts("  失效句柄反复释放时错误码变了");
		return false;
	}

	// ⑤ 弱句柄用例放在**独立文件** `BindingsWeakHandleTest.cpp` 里：两个生成物都各自产出
	// `BindScalarThingC` 镜像结构体，把 `IBindRow11.Bindings.cpp` 和 `IBindRow09.Bindings.cpp`
	// 塞进同一个翻译单元会 C2011（type redefinition）。

	// ⑥ 任务 4.1：代际回绕。代际是 16 位，同一槽被登记/释放 2^16 次之后会回到 0，
	// 最早那个句柄就会重新"有效"。现在的实现是"代际要归零就把槽退休"，
	// 所以跑完 2^16 轮之后，**最开始那个句柄 MUST 仍然失效**。
	{
		constexpr uint32_t kGenerationBits = 16u;
		constexpr uint32_t kCycles = 1u << kGenerationBits; // 65536
		uint64_t const firstHandle = ca_bind_fixture__IBindRow09__AcquireThing(&object);
		if (firstHandle == 0u)
		{
			std::puts("  回绕用例：第一轮就没拿到句柄");
			return false;
		}
		ca_bind_fixture__IBindRow09__ReleaseAcquireThing(&object, firstHandle);
		bool sawRepeat = false;
		for (uint32_t cycle = 0u; cycle < kCycles; ++cycle)
		{
			uint64_t const handle = ca_bind_fixture__IBindRow09__AcquireThing(&object);
			if (handle == 0u)
			{
				// 槽被退休之后表会继续往后长；长不出来才允许在这里退出
				break;
			}
			// 判据：**句柄数字永不重复**。代际回绕时同一个 (下标, 代际) 会再次被发出去 ——
			// 那正是"旧句柄复活"的定义；退休机制保证该槽在回绕前就不再分配。
			if (handle == firstHandle)
			{
				sawRepeat = true;
			}
			ca_bind_fixture__IBindRow09__ReleaseAcquireThing(&object, handle);
		}
		if (sawRepeat)
		{
			std::puts("  代际回绕：同一个句柄数字被重新发了出去（旧句柄会复活）");
			return false;
		}
		if (ca_bind_fixture__IBindRow09__ResolveHandle(&object, firstHandle) != nullptr)
		{
			std::puts("  代际回绕之后最早的句柄又复活了（退休机制没生效）");
			return false;
		}
	}

	// ⑦ 任务 4.3：句柄表现在有锁。8 个线程各做 1000 次「取 → 解析 → 释放」，要求
	// 不崩、每次解析都拿得到指针、结束后每个线程最后那个句柄都已失效。
	// 没锁的话：两个线程会挑中同一个空槽（同槽两用）、`push_back` 扩容时另一个线程
	// 正在写旧缓冲（写到已释放内存）、两个释放各读到同一代际只加一次（旧句柄提前复活）。
	{
		constexpr int kThreads = 8;
		constexpr int kPerThread = 1000;
		std::atomic<int> failures{ 0 };
		std::vector<uint64_t> lastHandles(static_cast<std::size_t>(kThreads), 0u);
		std::vector<std::thread> threads;
		threads.reserve(static_cast<std::size_t>(kThreads));
		for (int threadIndex = 0; threadIndex < kThreads; ++threadIndex)
		{
			threads.emplace_back([&object, &failures, &lastHandles, threadIndex]() {
				for (int iteration = 0; iteration < kPerThread; ++iteration)
				{
					uint64_t const handle = ca_bind_fixture__IBindRow09__AcquireThing(&object);
					if (handle == 0u)
					{
						++failures;
						return;
					}
					if (ca_bind_fixture__IBindRow09__ResolveHandle(&object, handle) == nullptr)
					{
						++failures;
					}
					ca_bind_fixture__IBindRow09__ReleaseAcquireThing(&object, handle);
					lastHandles[static_cast<std::size_t>(threadIndex)] = handle;
				}
			});
		}
		for (std::thread& thread : threads)
		{
			thread.join();
		}
		if (failures.load() != 0)
		{
			std::printf("  并发用例：%d 次调用失败（取不到句柄或解析拿到空）\n", failures.load());
			return false;
		}
		for (uint64_t handle : lastHandles)
		{
			if (handle != 0u && ca_bind_fixture__IBindRow09__ResolveHandle(&object, handle) != nullptr)
			{
				std::puts("  并发用例：已释放的句柄仍然能解析出指针");
				return false;
			}
		}
	}

	// ---- 任务 5.8：1002（实现返回空指针）的触发与码断言，外加"失败之后成功调用必须清零" ----
	{
		FakeRow09 nullObject;
		nullObject.returnNull = true;
		const uint64_t zeroHandle = ca_bind_fixture__IBindRow09__AcquireThing(&nullObject);
		if (zeroHandle != 0u)
		{
			std::puts("  实现返回空指针时应当拿到 0 句柄");
			return false;
		}
		if (ca_bind_fixture__IBindRow09__LastErrorCode(&nullObject) != ca_bind_fixture__IBindRow09_ERROR_NULL_POINTER)
		{
			std::printf("  实现返回空指针时应报 1002，实际 %d\n", ca_bind_fixture__IBindRow09__LastErrorCode(&nullObject));
			return false;
		}
		// 反例（D11 的"入口先清零"）：同一个实例紧接着一次成功调用，码必须回到 0
		nullObject.returnNull = false;
		if (ca_bind_fixture__IBindRow09__AcquireThing(&nullObject) == 0u || ca_bind_fixture__IBindRow09__LastErrorCode(&nullObject) != 0)
		{
			std::puts("  失败之后紧接着的成功调用没有把错误码清回 0");
			return false;
		}
	}

	return true;
}
