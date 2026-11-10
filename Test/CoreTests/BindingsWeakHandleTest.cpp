// 弱句柄（`csharp-window-creation` 任务 1.4）：`castl::weak_ptr<T>` 返回 ⇒ `uint64_t` 弱句柄。
//
// 为什么单独一个文件：生成物会各自产出 `BindScalarThingC` 镜像结构体，把
// `IBindRow11.Bindings.cpp` 和 `IBindRow09.Bindings.cpp` 塞进同一个翻译单元会 C2011
// （type redefinition），所以一个 TU 只能 include 一份产物。
//
// 两条读数，都必须能红：
//   ① 登记弱句柄**不抬高**引用计数（旧实现把 `weak_ptr` 当 ② 类拒绝，这条根本走不到）；
//   ② 对象被**持有方自己**回收后（全程不调任何释放入口），旧句柄必须判失效。
//      这条必须绕开 `ReleaseHandle`：走释放的话 1001 来自代际进位，`lock()` 那条分支
//      一行不写也全绿。

#include "BindableShapes.h"

#include "ca_bind_fixture__IBindRow11.Bindings.cpp"

#include <cstdio>
#include <memory>

namespace
{
	class FakeRow11 final : public ca_bind_fixture::IBindRow11
	{
	public:
		std::shared_ptr<ca_bind_fixture::BindScalarThing> m_owned =
			std::make_shared<ca_bind_fixture::BindScalarThing>();

		castl::weak_ptr<ca_bind_fixture::BindScalarThing> PeekThing() override
		{
			return m_owned;
		}
	};
}

bool RunBindingsWeakHandleTest()
{
	FakeRow11 owner;

	// ① 弱句柄不抬高引用计数
	const long before = static_cast<long>(owner.m_owned.use_count());
	const uint64_t handle = ca_bind_fixture__IBindRow11__PeekThing(&owner);
	if (handle == 0u)
	{
		std::puts("  弱句柄没有登记成功（返回 0）");
		return false;
	}
	const long after = static_cast<long>(owner.m_owned.use_count());
	if (after != before)
	{
		std::printf("  弱句柄抬高了引用计数：%ld → %ld（它应当只观察、不持有）\n", before, after);
		return false;
	}
	if (ca_bind_fixture__IBindRow11__ResolveHandle(&owner, handle) != owner.m_owned.get())
	{
		std::puts("  弱句柄解析出来的指针与实现持有的对象不一致");
		return false;
	}

	// ② 持有方自己把对象放掉（**不调 ReleaseHandle**），旧句柄必须立刻失效
	std::weak_ptr<ca_bind_fixture::BindScalarThing> witness = owner.m_owned; // 见证者：证明析构真的发生了
	owner.m_owned.reset();
	if (!witness.expired())
	{
		std::puts("  见证者没有过期 —— 对象其实还活着，下面的 1001 不能当证据");
		return false;
	}
	if (ca_bind_fixture__IBindRow11__ResolveHandle(&owner, handle) != nullptr)
	{
		std::puts("  对象已析构，弱句柄仍然给出了指针");
		return false;
	}
	if (ca_bind_fixture__IBindRow11__LastErrorCode(&owner) != 1001)
	{
		std::printf("  对象析构后弱句柄应报 1001，实际 %d\n", ca_bind_fixture__IBindRow11__LastErrorCode(&owner));
		return false;
	}
	return true;
}
