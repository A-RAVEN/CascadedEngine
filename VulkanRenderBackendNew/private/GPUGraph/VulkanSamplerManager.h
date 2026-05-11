#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <CACore/CASharedDic.h>
#include <TextureSampler.h>

namespace graphics_backend
{
	class VulkanSamplerManager : public VulkanSubobjectBase
	{
	public:
		VulkanSamplerManager() = default;

		// Get or create a vk::Sampler for the given descriptor. Cached globally.
		vk::Sampler GetOrCreateSampler(TextureSamplerDescriptor const& desc);

		// Destroy all cached samplers and clear the cache
		virtual void Release() override;

	private:
		castl::shared_dic<TextureSamplerDescriptor, vk::Sampler> m_SamplerCache;
	};
}
