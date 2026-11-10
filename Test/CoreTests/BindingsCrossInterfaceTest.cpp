// 跨头文件资源 / 发行接口的行为判据（csharp-render-binding-foundations 任务 1.2）。
//
// 判的是四件事：
//   ① 发行者入口与目标入口分别校验：资源句柄由 `IBindRenderOwner` 发行，`IBindRenderResource`
//      的发行者入口必须拒绝，目标入口必须解析到同一对象；
//   ② `shared_ptr<T>` 入参按目标类型反查重建：原生实现收到的是同一对象；错误目标类型不进入实现；
//   ③ 弱返回句柄不抬高 `use_count`，对象被回收后判失效（不依赖任何释放动作）；
//   ④ 借用返回的目标解析要求发行者见证存活；无见证 / 见证过期都拒绝，两类释放都能清表项并进位，
//      强句柄释放后表项里的 `strong` 为空。
//
// 测试直接 include 生成物，所以句柄表与错误通道在同一个翻译单元里可见（这是规范要求的判据口径）。
#include "ca_bind_fixture__IBindRenderOwner.Bindings.cpp"
#include "ca_bind_fixture__IBindRenderResource.Bindings.cpp"
#include "ca_bind_fixture__IBindRenderResourceAlt.Bindings.cpp"

#include <cstdio>
#include <cstring>

namespace
{
	class FakeOwner;

	class FakeResource final : public ca_bind_fixture::IBindRenderResource
	{
	public:
		FakeResource(castl::weak_ptr<ca_bind_fixture::IBindRenderOwner> ownerWeak,
			ca_bind_fixture::IBindRenderOwner* ownerRaw, uint32_t id)
			: m_ownerWeak(ownerWeak), m_ownerRaw(ownerRaw), m_id(id)
		{
		}

		uint32_t GetResourceId() const override
		{
			return m_id;
		}

		castl::string GetResourceName() const override
		{
			return castl::string("resource");
		}

		castl::weak_ptr<ca_bind_fixture::IBindRenderOwner> GetOwnerWeak() override
		{
			return m_ownerWeak;
		}

		ca_bind_fixture::IBindRenderOwner* GetOwnerBorrowed() override
		{
			return m_ownerRaw;
		}

	private:
		castl::weak_ptr<ca_bind_fixture::IBindRenderOwner> m_ownerWeak;
		ca_bind_fixture::IBindRenderOwner* m_ownerRaw = nullptr;
		uint32_t m_id = 0;
	};

	class FakeAltResource final : public ca_bind_fixture::IBindRenderResourceAlt
	{
	public:
		explicit FakeAltResource(uint32_t id) : m_id(id) {}
		uint32_t GetAltId() const override { return m_id; }
	private:
		uint32_t m_id = 0;
	};

	class FakeOwner final : public ca_bind_fixture::IBindRenderOwner
	{
	public:
		castl::shared_ptr<ca_bind_fixture::IBindRenderResource> CreateResource(uint32_t id) override
		{
			++createCalls;
			createdShared = castl::shared_ptr<ca_bind_fixture::IBindRenderResource>(
				new FakeResource(selfWeak, this, id));
			created = createdShared.get();
			return createdShared;
		}

		castl::weak_ptr<ca_bind_fixture::IBindRenderResource> PeekResource() override
		{
			return castl::weak_ptr<ca_bind_fixture::IBindRenderResource>(createdShared);
		}

		ca_bind_fixture::IBindRenderResource* BorrowResource() override
		{
			return createdShared.get();
		}

		void UseResource(castl::shared_ptr<ca_bind_fixture::IBindRenderResource> resource) override
		{
			++useCalls;
			lastUsed = resource.get();
			lastUsedId = resource ? resource->GetResourceId() : 0u;
		}

		void UseAltResource(castl::shared_ptr<ca_bind_fixture::IBindRenderResourceAlt>) override
		{
			++useAltCalls;
		}

		castl::shared_ptr<ca_bind_fixture::IBindRenderResourceAlt> CreateAltResource(uint32_t id) override
		{
			return castl::shared_ptr<ca_bind_fixture::IBindRenderResourceAlt>(new FakeAltResource(id));
		}

		uint32_t LastUsedId() const override { return lastUsedId; }

		castl::weak_ptr<ca_bind_fixture::IBindRenderOwner> selfWeak;
		castl::shared_ptr<ca_bind_fixture::IBindRenderResource> createdShared;
		ca_bind_fixture::IBindRenderResource* created = nullptr;
		ca_bind_fixture::IBindRenderResource* lastUsed = nullptr;
		ca_bind_fixture::IBindRenderResourceAlt* lastAlt = nullptr;
		uint32_t lastUsedId = 0;
		int createCalls = 0;
		int useCalls = 0;
		int useAltCalls = 0;
	};

	bool Expect(bool condition, char const* what)
	{
		if (!condition)
		{
			std::printf("  [跨接口] 失败：%s\n", what);
		}
		return condition;
	}

	uint64_t HandleIndex(uint64_t handle, uint32_t indexBits)
	{
		return handle & ((1ull << indexBits) - 1ull);
	}
}

bool RunBindingsCrossInterfaceTest()
{
	auto ownerShared = castl::shared_ptr<FakeOwner>(new FakeOwner());
	FakeOwner* owner = ownerShared.get();
	owner->selfWeak = castl::weak_ptr<ca_bind_fixture::IBindRenderOwner>(ownerShared);
	bool ok = true;

	// ---- ① 强返回：发行者入口 / 目标入口分别校验 ----
	const uint64_t strong = ca_bind_fixture__IBindRenderOwner__CreateResource(owner, 42u);
	ok = Expect(strong != 0ull && owner->createCalls == 1, "CreateResource 没有拿到句柄") && ok;
	ok = Expect(owner->created != nullptr, "原生实现没有拿到创建出来的资源") && ok;
	ok = Expect(ca_bind_fixture__IBindRenderResource__ResolveHandle(owner, strong) == nullptr &&
		ca_bind_fixture__IBindRenderResource__LastErrorCode(owner) == ca_bind_fixture__IBindRenderResource_ERROR_STALE_HANDLE,
		"资源接口的发行者入口没有拒绝别的接口发行的句柄") && ok;
	ok = Expect(ca_bind_fixture__IBindRenderOwner__ResolveHandle(owner, strong) == owner->created,
		"发行者入口没有解析到发行者自己的句柄") && ok;
	ok = Expect(ca_bind_fixture__IBindRenderResource__ResolveTargetHandle(owner, strong) == owner->created,
		"资源目标入口没有按目标类型解析跨头文件句柄") && ok;
	ok = Expect(ca_bind_fixture__IBindRenderOwner__ResolveTargetHandle(owner, strong) == nullptr &&
		ca_bind_fixture__IBindRenderOwner__LastErrorCode(owner) == ca_bind_fixture__IBindRenderOwner_ERROR_STALE_HANDLE,
		"目标入口没有拒绝错误的目标类型") && ok;

	// ---- ② shared_ptr 入参按目标类型反查重建 ----
	ca_bind_fixture__IBindRenderOwner__UseResource(owner, strong);
	ok = Expect(owner->useCalls == 1 && owner->lastUsed == owner->created,
		"shared_ptr 入参没有重建到同一对象") && ok;
	const int useCallsBefore = owner->useCalls;
	ca_bind_fixture__IBindRenderOwner__UseAltResource(owner, strong);
	ok = Expect(owner->useAltCalls == 0 &&
		ca_bind_fixture__IBindRenderOwner__LastErrorCode(owner) == ca_bind_fixture__IBindRenderOwner_ERROR_STALE_HANDLE,
		"错误目标类型的 shared_ptr 入参进入了原生实现") && ok;
	ok = Expect(owner->useCalls == useCallsBefore, "错误目标类型的调用改动了原生实现状态") && ok;

	// ---- ③ 弱返回：不抬高引用计数，对象回收后判失效 ----
	const long useCountBefore = owner->createdShared.use_count();
	const uint64_t weak = ca_bind_fixture__IBindRenderOwner__PeekResource(owner);
	const long useCountAfter = owner->createdShared.use_count();
	ok = Expect(weak != 0ull && useCountBefore == useCountAfter,
		"弱返回句柄抬高了 use_count") && ok;
	ok = Expect(ca_bind_fixture__IBindRenderResource__ResolveTargetHandle(owner, weak) == owner->created,
		"弱返回句柄在对象存活时解析失败") && ok;
	// 先释放强句柄（表项里的强引用），再让实现自己回收对象：只有两步都做完，弱句柄才该判失效。
	ca_bind_fixture__IBindRenderOwner__ReleaseCreateResource(owner, strong);
	ok = Expect(ca_bind_fixture__IBindRenderResource__ResolveTargetHandle(owner, weak) == owner->created,
		"强句柄释放后、对象仍被持有者保活时弱句柄不该失效") && ok;
	owner->createdShared.reset();
	ok = Expect(ca_bind_fixture__IBindRenderResource__ResolveTargetHandle(owner, weak) == nullptr &&
		ca_bind_fixture__IBindRenderResource__LastErrorCode(owner) == ca_bind_fixture__IBindRenderResource_ERROR_STALE_HANDLE,
		"对象回收后弱返回句柄没有判失效") && ok;
	ca_bind_fixture__IBindRenderResource__ReleaseTargetHandle(owner, weak);
	{
		const uint64_t index = HandleIndex(weak, ca_bind_fixture__IBindRenderOwner_HANDLE_INDEX_BITS);
		ok = Expect(index >= 1ull && index <= ca_bind_fixture__IBindRenderOwner_handles.size() &&
			ca_bind_fixture__IBindRenderOwner_handles[index - 1u].kind == 0u,
			"目标释放没有清掉弱句柄的表项") && ok;
	}

	// ---- ④ 借用 + 见证：见证存活时目标解析成功，见证过期后拒绝 ----
	const uint64_t borrowedIssuer = ca_bind_fixture__IBindRenderOwner__CreateResource(owner, 7u);
	void* resourceSelf = ca_bind_fixture__IBindRenderResource__ResolveTargetHandle(owner, borrowedIssuer);
	ok = Expect(resourceSelf != nullptr, "借用场景拿不到资源对象") && ok;
	const uint64_t ownerWeak = ca_bind_fixture__IBindRenderResource__GetOwnerWeak(resourceSelf);
	ok = Expect(ca_bind_fixture__IBindRenderOwner__ResolveTargetHandle(owner, ownerWeak) == owner,
		"弱返回的跨头文件目标解析失败") && ok;
	const uint64_t borrowed = ca_bind_fixture__IBindRenderResource__GetOwnerBorrowed(resourceSelf);
	ok = Expect(borrowed != 0ull, "借用返回没有拿到句柄") && ok;
	ok = Expect(ca_bind_fixture__IBindRenderOwner__ResolveTargetHandle(owner, borrowed) == owner,
		"见证存活时借用句柄目标解析失败") && ok;
	ca_bind_fixture__IBindRenderOwner__ReleaseCreateResource(owner, borrowedIssuer);
	ok = Expect(ca_bind_fixture__IBindRenderOwner__ResolveTargetHandle(owner, borrowed) == nullptr &&
		ca_bind_fixture__IBindRenderOwner__LastErrorCode(owner) == ca_bind_fixture__IBindRenderOwner_ERROR_STALE_HANDLE,
		"见证过期后借用句柄目标解析没有被拒绝") && ok;
	ca_bind_fixture__IBindRenderOwner__ReleaseTargetHandle(owner, borrowed);
	ok = Expect(ca_bind_fixture__IBindRenderOwner__ResolveTargetHandle(owner, borrowed) == nullptr,
		"借用句柄释放后仍然可以解析") && ok;

	// ---- ⑤ 无见证借用：栈上的资源对象不在表里 ----
	FakeResource stackResource(castl::weak_ptr<ca_bind_fixture::IBindRenderOwner>(ownerShared), owner, 3u);
	const uint64_t noWitness = ca_bind_fixture__IBindRenderResource__GetOwnerBorrowed(&stackResource);
	ok = Expect(noWitness != 0ull, "无见证借用没有拿到句柄") && ok;
	ok = Expect(ca_bind_fixture__IBindRenderOwner__ResolveTargetHandle(owner, noWitness) == nullptr &&
		ca_bind_fixture__IBindRenderOwner__LastErrorCode(owner) == ca_bind_fixture__IBindRenderOwner_ERROR_STALE_HANDLE,
		"无见证借用句柄通过了目标解析") && ok;
	ca_bind_fixture__IBindRenderOwner__ReleaseTargetHandle(owner, noWitness);
	{
		const uint64_t index = HandleIndex(noWitness, ca_bind_fixture__IBindRenderOwner_HANDLE_INDEX_BITS);
		ok = Expect(index >= 1ull && index <= ca_bind_fixture__IBindRenderOwner_handles.size() &&
			ca_bind_fixture__IBindRenderOwner_handles[index - 1u].kind == 0u,
			"无见证借用释放没有清掉表项") && ok;
	}

	// ---- ⑥ 强句柄释放后表项强引用为空，旧句柄失效且不再进入实现 ----
	const uint64_t released = ca_bind_fixture__IBindRenderOwner__CreateResource(owner, 9u);
	const uint64_t releasedIndex = HandleIndex(released, ca_bind_fixture__IBindRenderOwner_HANDLE_INDEX_BITS);
	ca_bind_fixture__IBindRenderOwner__ReleaseCreateResource(owner, released);
	ok = Expect(releasedIndex >= 1ull && releasedIndex <= ca_bind_fixture__IBindRenderOwner_handles.size() &&
		ca_bind_fixture__IBindRenderOwner_handles[releasedIndex - 1u].kind == 0u &&
		ca_bind_fixture__IBindRenderOwner_handles[releasedIndex - 1u].strong == nullptr,
		"释放后表项仍持有强引用") && ok;
	const int useCallsAfterRelease = owner->useCalls;
	ca_bind_fixture__IBindRenderOwner__UseResource(owner, released);
	ok = Expect(owner->useCalls == useCallsAfterRelease &&
		ca_bind_fixture__IBindRenderOwner__LastErrorCode(owner) == ca_bind_fixture__IBindRenderOwner_ERROR_STALE_HANDLE,
		"释放后的旧句柄进入了原生实现") && ok;

	// ---- ⑦ Alt 资源同样按目标类型解析 ----
	const uint64_t alt = ca_bind_fixture__IBindRenderOwner__CreateAltResource(owner, 11u);
	ok = Expect(alt != 0ull, "Alt 资源没有拿到句柄") && ok;
	ok = Expect(ca_bind_fixture__IBindRenderResourceAlt__ResolveTargetHandle(owner, alt) != nullptr,
		"Alt 目标入口没有解析自己的句柄") && ok;
	ok = Expect(ca_bind_fixture__IBindRenderResource__ResolveTargetHandle(owner, alt) == nullptr &&
		ca_bind_fixture__IBindRenderResource__LastErrorCode(owner) == ca_bind_fixture__IBindRenderResource_ERROR_STALE_HANDLE,
		"资源目标入口没有拒绝 Alt 句柄") && ok;
	ca_bind_fixture__IBindRenderOwner__ReleaseCreateAltResource(owner, alt);

	return ok;
}
