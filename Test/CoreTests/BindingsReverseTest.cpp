// 反向投影的结构判据（csharp-reverse-interface-binding 3.1 / 8.2）：C# 实现 C++ 接口的 C++ 侧形状。
//
// 这里**不加载 C#**（CoreTests 没有 hostfxr）：用一组 C 函数指针当"假实现"，直接调生成的注册 glue，
// 断言的是结构 —— 代理继承关系、句柄表表项 kind、每条被绑定虚方法真的被调到、跳过的方法抛 1013、
// releaseUser 只被调一次、反向保活计数 1 → 0、旧句柄按 1001 拒绝。
// **行为**（真的 C# 覆写被调到、收返回值）在 GPUBackendTester 的 TestCSharpResourceFactory 里。
//
// 红态怎么构造：把生成的代理方法体改成空体 → `impl.*Calls` 不涨；把析构里的 `release(m_user)` 删掉
// → ReleaseCalls 停在 0；把 TakeOwnership 里的 `entry.kind = 4` 删掉 → 表项 kind 断言红。

#include "ca_bind_fixture__IBindReverseShape.Bindings.cpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace
{
	struct FakeReverseImpl
	{
		int AddCalls = 0;
		int DescribeCalls = 0;
		int ResetCalls = 0;
		int ReadyCalls = 0;
		int ReleaseCalls = 0;
		bool Ready = true;
	};

	int32_t FakeAdd(void* user, int32_t left, int32_t right)
	{
		++static_cast<FakeReverseImpl*>(user)->AddCalls;
		return left + right;
	}

	uint64_t FakeDescribeLen(void* user)
	{
		++static_cast<FakeReverseImpl*>(user)->DescribeCalls;
		return 7u;
	}

	uint64_t FakeCopyDescribe(void* user, char* destination, uint64_t capacity)
	{
		(void)user;
		if (destination == nullptr || capacity < 7u)
		{
			return UINT64_MAX;
		}
		std::memcpy(destination, "reverse", 7u);
		return 7u;
	}

	void FakeReset(void* user)
	{
		++static_cast<FakeReverseImpl*>(user)->ResetCalls;
	}

	int32_t FakeIsReady(void* user)
	{
		FakeReverseImpl* impl = static_cast<FakeReverseImpl*>(user);
		++impl->ReadyCalls;
		return impl->Ready ? 1 : 0;
	}

	uint64_t FakeTakeLastError(void* user, char* destination, uint64_t capacity)
	{
		(void)user;
		(void)destination;
		(void)capacity;
		return 0u;
	}

	void FakeRelease(void* user)
	{
		++static_cast<FakeReverseImpl*>(user)->ReleaseCalls;
	}

	// 句柄表读表项 kind（句柄 = 代际 << 48 | 序号；共用一张表，表项带接口标签）。
	uint32_t HandleKind(uint64_t handle)
	{
		constexpr uint64_t kIndexMask = (1ull << 48) - 1ull;
		const uint64_t index = handle & kIndexMask;
		auto& table = cascaded_bindings::HandleTable();
		if (index == 0u || index > table.size())
		{
			return 0u;
		}
		return table[index - 1u].kind;
	}

	struct ReverseTestFailure : std::runtime_error
	{
		using std::runtime_error::runtime_error;
	};

	void Require(bool condition, std::string const& text)
	{
		if (!condition)
		{
			throw ReverseTestFailure(text);
		}
	}
}

bool RunBindingsReverseTest()
{
	try
	{
		// ---- 结构：代理必须能实例化（每个被绑定虚方法都有实现），且确实继承夹具接口 ----
		static_assert(std::is_base_of_v<ca_bind_fixture::IBindReverseShape,
		                                ca_bind_fixture__IBindReverseShapeProxy>,
		              "反向代理必须继承被绑定接口");
		static_assert(!std::is_abstract_v<ca_bind_fixture__IBindReverseShapeProxy>,
		              "反向代理必须能实例化（被绑定虚方法在代理里都有实现）");
		static_assert(std::is_abstract_v<ca_bind_fixture::IBindReverseShape>,
		              "反向接口自身仍是抽象类（C# 继承生成的抽象基类去实现它）");

		FakeReverseImpl impl;
		const uint64_t handle = ca_bind_fixture__IBindReverseShape__CreateImpl(
			&impl, &FakeAdd, &FakeDescribeLen, &FakeCopyDescribe, &FakeReset, &FakeIsReady,
			&FakeTakeLastError, &FakeRelease);
		Require(handle != 0u, "CreateImpl 没有返回句柄");
		Require(HandleKind(handle) == 1u, "CreateImpl 之后表项应是强句柄（kind 1），实际 " +
		                                      std::to_string(HandleKind(handle)));
		Require(ca_bind_fixture__IBindReverseShape__ReverseKeepAliveCount(handle) == 1u,
		        "创建之后反向保活计数应当是 1");

		// ---- 单一所有者：TakeOwnership 把强引用移出句柄表，表项降级成借用（kind 4）----
		std::shared_ptr<ca_bind_fixture::IBindReverseShape> owned =
			ca_bind_fixture__IBindReverseShape__TakeOwnership(handle);
		Require(owned != nullptr, "TakeOwnership 取不到代理（强句柄没被接走）");
		Require(HandleKind(handle) == 4u,
		        "TakeOwnership 之后表项应降级成借用（kind 4），实际 " + std::to_string(HandleKind(handle)));

		// ---- 对齐：每条被绑定虚方法都真的调到实现并收到返回值 ----
		Require(owned->Add(2, 3) == 5, "Add 的返回值没有从函数指针槽传回来");
		Require(impl.AddCalls == 1, "Add 的槽没有被调用（代理空体？）");
		Require(owned->Describe() == "reverse", "Describe 的两段式字符串没有正确拼回");
		Require(impl.DescribeCalls == 1, "DescribeLen 的槽没有被调用");
		owned->Reset();
		Require(impl.ResetCalls == 1, "Reset 的槽没有被调用");
		Require(owned->IsReady() == true, "bool 返回值没有按 int32 还原");
		Require(impl.ReadyCalls == 1, "IsReady 的槽没有被调用");

		// ---- 被 CA_BIND_SKIP 跳过的纯虚方法：代理抛 1013，不进实现 ----
		{
			bool threw = false;
			try
			{
				owned->Deserialize(nullptr);
			}
			catch (cascaded_reverse::ReverseBindingError const& error)
			{
				threw = error.Code() == 1013;
				Require(threw, "跳过的 Deserialize 抛的错误码是 " + std::to_string(error.Code()) +
				                   "，期望 1013（REVERSE_READ_UNSUPPORTED）");
			}
			Require(threw, "跳过的 Deserialize 没有抛 ReverseBindingError");
		}

		// ---- 寿命：析构调 releaseUser 恰好一次、保活计数归零、旧句柄按 1001 拒 ----
		owned.reset();
		Require(impl.ReleaseCalls == 1, "代理析构时 releaseUser 没有被调（或调了不止一次）：" +
		                                   std::to_string(impl.ReleaseCalls));
		Require(ca_bind_fixture__IBindReverseShape__ReverseKeepAliveCount(handle) == 0u,
		        "代理析构之后反向保活计数应当是 0");
		Require(ca_bind_fixture__IBindReverseShape__ReleaseImpl(handle) == 1001,
		        "代理析构之后旧句柄没有按 1001 拒绝");
		Require(ca_bind_fixture__IBindReverseShape__ReleaseImpl(handle) == 1001,
		        "重复释放同一个旧句柄不是幂等的 1001（double-free 守卫）");

		// ---- 失败契约：长度槽返回哨兵 → 代理落槽、下一次调用抛 1012 ----
		// 用一个"长度失败"的假实现：DescribeLen 返回 UINT64_MAX。
		// 这里单独建一个实例，避免污染上面的调用计数。
		{
			struct FailingImpl
			{
				int ReleaseCalls = 0;
			} failing;
			auto failingLen = [](void*) -> uint64_t { return UINT64_MAX; };
			// 注意：这里必须**真的计数**。第一版写成空 lambda `[](void*){}`，于是
			// `failing.ReleaseCalls == 1` 永远不成立 —— 那是测试自己的红，不是生成器漏调
			// releaseUser（实测：CoreTests 报"失败场景的代理析构没有调 releaseUser"）。
			auto failingRelease = [](void* user) { ++static_cast<FailingImpl*>(user)->ReleaseCalls; };
			const uint64_t failingHandle = ca_bind_fixture__IBindReverseShape__CreateImpl(
				&failing, &FakeAdd, +failingLen, &FakeCopyDescribe, &FakeReset, &FakeIsReady,
				&FakeTakeLastError, +failingRelease);
			Require(failingHandle != 0u, "失败场景的 CreateImpl 没有返回句柄");
			auto failingOwned = ca_bind_fixture__IBindReverseShape__TakeOwnership(failingHandle);
			Require(failingOwned != nullptr, "失败场景的 TakeOwnership 取不到代理");
			// 第一次调用：长度哨兵 → 代理中止、落槽（不抛）。
			(void)failingOwned->Describe();
			bool threw = false;
			try
			{
				(void)failingOwned->Describe();
			}
			catch (cascaded_reverse::ReverseBindingError const& error)
			{
				threw = error.Code() == 1012;
				Require(threw, "长度失败之后的下一次调用抛的是 " + std::to_string(error.Code()) +
				                   "，期望 1012（REVERSE_EXCEPTION）");
			}
			Require(threw, "长度失败之后的下一次调用没有抛（静默继续）");
			failingOwned.reset();
			Require(failing.ReleaseCalls == 1, "失败场景的代理析构没有调 releaseUser");
		}

		return true;
	}
	catch (std::exception const& error)
	{
		std::printf("  [反向夹具] %s\n", error.what());
		return false;
	}
}
