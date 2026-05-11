#include <GPUGraph/VulkanSamplerManager.h>

namespace graphics_backend
{
	// Convert TextureSamplerDescriptor to vk::SamplerCreateInfo
	static vk::SamplerCreateInfo MakeSamplerCreateInfo(TextureSamplerDescriptor const& desc)
	{
		vk::SamplerCreateInfo info{};
		info.magFilter = desc.magFilterMode == ETextureSamplerFilterMode::eLinear ? vk::Filter::eLinear : vk::Filter::eNearest;
		info.minFilter = desc.minFilterMode == ETextureSamplerFilterMode::eLinear ? vk::Filter::eLinear : vk::Filter::eNearest;
		info.mipmapMode = desc.mipmapFilterMode == ETextureSamplerFilterMode::eLinear ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest;

		auto mapAddressMode = [](ETextureSamplerAddressMode mode) -> vk::SamplerAddressMode {
			switch (mode)
			{
			case ETextureSamplerAddressMode::eRepeat:          return vk::SamplerAddressMode::eRepeat;
			case ETextureSamplerAddressMode::eMirroredRepeat:  return vk::SamplerAddressMode::eMirroredRepeat;
			case ETextureSamplerAddressMode::eClampToEdge:     return vk::SamplerAddressMode::eClampToEdge;
			case ETextureSamplerAddressMode::eClampToBorder:   return vk::SamplerAddressMode::eClampToBorder;
			default:                                           return vk::SamplerAddressMode::eRepeat;
			}
		};
		info.addressModeU = mapAddressMode(desc.addressModeU);
		info.addressModeV = mapAddressMode(desc.addressModeV);
		info.addressModeW = mapAddressMode(desc.addressModeW);

		return info;
	}

	vk::Sampler VulkanSamplerManager::GetOrCreateSampler(TextureSamplerDescriptor const& desc)
	{
		return m_SamplerCache.get_or_create(desc, [&]() -> vk::Sampler {
			return GetDevice().createSampler(MakeSamplerCreateInfo(desc));
		})->second;
	}

	void VulkanSamplerManager::Release()
	{
		m_SamplerCache.clear([&](TextureSamplerDescriptor const&, vk::Sampler& sampler) {
			if (sampler)
			{
				GetDevice().destroySampler(sampler);
			}
		});
	}
}
