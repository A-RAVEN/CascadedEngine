#pragma once
#include <Utils/VulkanIncludes.h>
#include <CACore/CASharedDic.h>
#include <Utils/VulkanSubobjectBase.h>
#include <PipelineStates/FragmentOutputStates.h>

namespace graphics_backend
{
	class FragmentOutputState : public VulkanSubobjectBase
	{
		FragmentOutputStateCache m_StateCache;
		vk::Pipeline m_Library = nullptr;
	public:
		// F31: Init removed — declared but never defined anywhere in the repo (dead code).
		vk::Pipeline const& GetLibrary() const { return m_Library; }
		FragmentOutputStateCache const& GetCache() const { return m_StateCache; }
	};


	class FragmentOutputStateManager : public VulkanSubobjectBase
	{
	public:
		// F31a: Ensure/Get declared but never defined (no .cpp) and never called —
		// same dead-code class as Init above. Kept for future GPL fragment-output work.
		FragmentOutputState const& EnsureFragmentOutputState(FragmentOutputStateCache const& cacheData);
		FragmentOutputState const& GetFragmentOutputState(TypedVKHashVal<FragmentOutputStateCache> const& hashVal) const;
	private:
		castl::shared_dic<TypedVKHashVal<FragmentOutputStateCache>, FragmentOutputState> m_StateDesc;
	};
}