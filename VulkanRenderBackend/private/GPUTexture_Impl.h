#pragma once
#include <RenderInterface/header/GPUTexture.h>
#include "VulkanApplicationSubobjectBase.h"
#include "VulkanImageObject.h"
#include "TickUploadingResource.h"

namespace graphics_backend
{
	class GPUTexture_Impl : public BaseTickingUpdateResource, public GPUTexture
	{
	public:
		GPUTexture_Impl(CVulkanApplication& app);
		virtual void ScheduleTextureData(uint64_t textureDataOffset, uint64_t dataSize, void const* pData) override;
		virtual bool UploadingDone() const override;
		virtual GPUTextureDescriptor const& GetDescriptor() const override { return m_Descriptor; }
		void Initialize(const GPUTextureDescriptor& descriptor);
		void TickUpload() override;
		virtual void Release() override;
		vk::ImageView GetDefaultImageView() const { return m_DefaultImageView; }
		vk::ImageView GetDepthImageView() const { return m_DepthImageView; }
	private:
		GPUTextureDescriptor m_Descriptor;
		VulkanImageObject m_ImageObject;
		vk::ImageView m_DefaultImageView;
		vk::ImageView m_DepthImageView;
		vk::ImageView m_StencilImageView;
		std::vector<char> m_ScheduledData;
	};
}

