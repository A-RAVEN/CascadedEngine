#pragma once
#include <GPUTexture.h>
#include <Utils/VulkanSubobjectBase.h>
#include <vk_mem_alloc.h>

namespace graphics_backend
{
	class VulkanTexture : public GPUTexture, public VulkanSubobjectBase
	{
	public:
		VulkanTexture() = default;
		VulkanTexture(VulkanTexture&& other) noexcept = default;
		VulkanTexture& operator=(VulkanTexture&& other) noexcept = default;

		void Init(GPUTextureDescriptor const& descriptor, ETextureAccessTypeFlags accessType);
		virtual void Release() override;

		// GPUTexture interface
		virtual GPUTextureDescriptor const& GetDescriptor() const override { return m_Descriptor; }
		virtual void SetName(castl::string const& name) override;
		virtual castl::string const& GetName() const override { return m_Name; }

		// Vulkan-specific methods
		vk::Image GetImage() const { return m_Image; }
		vk::ImageView GetImageView() const { return m_ImageView; }
		VmaAllocation GetAllocation() const { return m_Allocation; }

		// Image layout management
		vk::ImageLayout GetCurrentLayout() const { return m_CurrentLayout; }
		void SetCurrentLayout(vk::ImageLayout layout) { m_CurrentLayout = layout; }

		// Transition image layout
		void TransitionLayout(vk::CommandBuffer cmdBuf, vk::ImageLayout newLayout
			, vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe
			, vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eBottomOfPipe);

		// Upload texture data via staging buffer
		void UploadData(void const* pData, uint64_t size);

		// Get aspects for depth/stencil
		vk::ImageAspectFlags GetImageAspect() const;

	private:
		// Helper to convert texture format
		static vk::Format ConvertFormat(ETextureFormat format);
		static vk::ImageType ConvertTextureType(ETextureType type);
		static vk::SampleCountFlagBits ConvertSampleCount(EMultiSampleCount samples);

		vk::Image m_Image;
		vk::ImageView m_ImageView;
		VmaAllocation m_Allocation = VK_NULL_HANDLE;
		GPUTextureDescriptor m_Descriptor{};
		ETextureAccessTypeFlags m_AccessType{};
		castl::string m_Name;
		vk::ImageLayout m_CurrentLayout = vk::ImageLayout::eUndefined;
	};
}
