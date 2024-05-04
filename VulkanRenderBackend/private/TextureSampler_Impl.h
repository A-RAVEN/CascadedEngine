#pragma once
#include <TextureSampler.h>
#include "HashPool.h"
#include "VulkanApplicationSubobjectBase.h"

namespace graphics_backend
{
	class TextureSampler_Impl : public TextureSampler, public VKAppSubObjectBaseNoCopy
	{
	public:
		TextureSampler_Impl(CVulkanApplication& app);
		virtual TextureSamplerDescriptor const& GetDescriptor() const override { return m_Descriptor; }
		void Create(TextureSamplerDescriptor const& descriptor);
		void Initialize(TextureSamplerDescriptor const& descriptor);
		virtual void Release() override;

		vk::Sampler GetSampler() const { return m_Sampler; }
	private:
		TextureSamplerDescriptor m_Descriptor;
		vk::Sampler m_Sampler;
	};

	using TextureSamplerObjectDic = HashPool<TextureSamplerDescriptor, TextureSampler_Impl>;
}


