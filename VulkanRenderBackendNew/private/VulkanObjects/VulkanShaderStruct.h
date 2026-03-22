#pragma once
#include <ShaderStruct.h>
#include <Utils/VulkanSubobjectBase.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAVector.h>

namespace graphics_backend
{
	class VulkanShaderStruct : public ShaderStruct, public VulkanSubobjectBase
	{
	public:
		VulkanShaderStruct() = default;
		VulkanShaderStruct(VulkanShaderStruct&& other) noexcept = default;
		VulkanShaderStruct& operator=(VulkanShaderStruct&& other) noexcept = default;

		void Init(cacore::NameHash const& structType);
		virtual void Release() override;

		// ShaderStruct interface
		virtual void SetValueInternal(cacore::NameHash const& name
			, void const* pValue
			, uint32_t sizeInBytes
			, uint32_t elementIndex) override;

		virtual void SetImageInternal(cacore::NameHash const& name
			, ImageHandle const& imageHandle, GPUTextureView const& view
			, uint32_t elementIndex) override;

		virtual void SetBufferInternal(cacore::NameHash const& name
			, BufferHandle const& bufferHandle
			, uint32_t elementIndex) override;

		virtual void SetSamplerInternal(cacore::NameHash const& name
			, TextureSamplerDescriptor const& samplerDesc
			, uint32_t elementIndex) override;

		virtual void SetStructInternal(cacore::NameHash const& name
			, castl::shared_ptr<ShaderStruct> const& subStruct
			, uint32_t elementIndex) override;

		virtual cacore::NameHash const& GetStructTypeName() const override { return m_StructTypeName; }

		// Vulkan-specific methods
		vk::DescriptorSetLayout GetDescriptorSetLayout() const { return m_DescriptorSetLayout; }
		vk::PipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }

		// Get descriptor set for binding
		vk::DescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }

		// Flush updates to GPU
		void FlushUpdates();

	private:
		cacore::NameHash m_StructTypeName;

		vk::DescriptorSetLayout m_DescriptorSetLayout;
		vk::PipelineLayout m_PipelineLayout;
		vk::DescriptorPool m_DescriptorPool;
		vk::DescriptorSet m_DescriptorSet;

		// Pending updates
		struct PendingUpdate
		{
			enum class Type { Value, Image, Buffer, Sampler, Struct };
			Type type;
			cacore::NameHash name;
			uint32_t elementIndex;
			castl::vector<uint8_t> data;
			ImageHandle imageHandle;
			GPUTextureView textureView;
			BufferHandle bufferHandle;
			TextureSamplerDescriptor samplerDesc;
			castl::shared_ptr<ShaderStruct> subStruct;
		};

		castl::vector<PendingUpdate> m_PendingUpdates;
	};
}
