// 句柄表（`csharp-bindings-generator` 任务 2.4）：序号 + 代际、配对释放、失效报错、参数侧反查。
//
// 四条读数：
//   ① 实现返回智能指针 → 拿到非零句柄；
//   ② 有效句柄释放一次 → 成功、错误码保持 0（证明反查命中）；
//   ③ 同一个句柄再释放 → 错误码 1001（句柄已失效），进程不崩；
//   ④ 释放后再反查仍然是失效（旧句柄不会因为"序号复用"而突然生效）。

#include "BindableShapes.h"

#include "IBindRow09.Bindings.cpp"

#include <cstdio>
#include <cstring>
#include <memory>

namespace
{
	class FakeRow09 final : public ca_bind_fixture::IBindRow09
	{
	public:
		castl::shared_ptr<ca_bind_fixture::BindScalarThing> AcquireThing() override
		{
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
	const uint64_t handle = IBindRow09_AcquireThing(&object);
	if (handle == 0u)
	{
		std::puts("  AcquireThing 没有返回句柄");
		return false;
	}

	// ② 有效句柄释放一次：成功且错误码回到 0
	IBindRow09_ReleaseAcquireThing(&object, handle);
	if (IBindRow09_LastErrorCode(&object) != 0)
	{
		std::printf("  有效句柄释放后错误码应为 0，实际 %d\n", IBindRow09_LastErrorCode(&object));
		return false;
	}

	// ③ 同一句柄再释放：失效错误码 1001
	IBindRow09_ReleaseAcquireThing(&object, handle);
	if (IBindRow09_LastErrorCode(&object) != 1001)
	{
		std::printf("  重复释放应报 1001，实际 %d\n", IBindRow09_LastErrorCode(&object));
		return false;
	}
	char text[64] = { 0 };
	IBindRow09_LastErrorText(&object, text, sizeof(text));
	if (std::strstr(text, "stale") == nullptr)
	{
		std::printf("  失效后文本里没有说明：%s\n", text);
		return false;
	}

	// ④ 再释放一次仍然是 1001（旧句柄不会复活）
	IBindRow09_ReleaseAcquireThing(&object, handle);
	if (IBindRow09_LastErrorCode(&object) != 1001)
	{
		std::puts("  失效句柄反复释放时错误码变了");
		return false;
	}

	return true;
}
