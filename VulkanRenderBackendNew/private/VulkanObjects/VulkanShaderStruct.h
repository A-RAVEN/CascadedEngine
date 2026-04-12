#pragma once
#include <ShaderStruct.h>
#include <Utils/VulkanSubobjectBase.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAVector.h>
#include <Compiler.h>
#include <Common.h>

namespace graphics_backend
{
	class VulkanShaderStruct : public ShaderStruct, public VulkanSubobjectBase
	{
	public:
		VulkanShaderStruct() = default;
		VulkanShaderStruct(VulkanShaderStruct&& other) noexcept = default;
		VulkanShaderStruct& operator=(VulkanShaderStruct&& other) noexcept = default;

		void Init(ShaderCompilerSlang::ShaderStructData const* pStructData);
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

		// Version control
		uint64_t ComputeMaxChildrenVersion() const;

		// Uniform buffer access (Phase 6)
		void UpdateUniformBuffer(uint64_t uniformBufferVersion, void* pOutBuffer, uint32_t bufferSize, uint32_t offset) const;
		ShaderCompilerSlang::ShaderStructData const* GetStructData() const { return p_StructData; }
		uint64_t GetCBufferSize() const;

		// Resource accessors (Phase 8)
		castl::unordered_map<cacore::NameHash, castl::vector<castl::pair<ImageHandle, GPUTextureView>>> const& GetImageHandles() const { return m_NameToImageHandles; }
		castl::unordered_map<cacore::NameHash, castl::vector<BufferHandle>> const& GetBufferHandles() const { return m_NameToBufferHandles; }
		castl::unordered_map<cacore::NameHash, castl::vector<TextureSamplerDescriptor>> const& GetSamplerDescriptors() const { return m_NameToSamplerDescriptors; }
		castl::unordered_map<cacore::NameHash, castl::vector<castl::shared_ptr<VulkanShaderStruct>>> const& GetSubStructs() const { return m_NameToSubStructs; }
		ETextureAccessType GetTextureAccessType(cacore::NameHash const& textureName) const;
		ShaderCompilerSlang::EShaderResourceAccess GetBufferRWType(cacore::NameHash const& bufferName) const;

	private:
		// Version control
		uint64_t m_Version = 0;
		mutable uint64_t m_MaxChildrenVersion = 0;
		void UpdateVersion();
		cacore::NameHash m_StructTypeName;

		// Struct data reference (Phase 6)
		ShaderCompilerSlang::ShaderStructData const* p_StructData = nullptr;

		// Uniform buffer staging (Phase 6)
		castl::vector<uint8_t> m_StructLocalUniformStagingBuffer;
		castl::unordered_map<cacore::NameHash, uint32_t> m_NameToUniformElementMetaID;
		castl::vector<uint64_t> m_UniformElementOffsetInStagingBuffer;

		vk::DescriptorSetLayout m_DescriptorSetLayout;
		vk::PipelineLayout m_PipelineLayout;
		vk::DescriptorPool m_DescriptorPool;
		vk::DescriptorSet m_DescriptorSet;

		// Resource handle storage (Phase 7)
		castl::unordered_map<cacore::NameHash, castl::vector<castl::pair<ImageHandle, GPUTextureView>>> m_NameToImageHandles;
		castl::unordered_map<cacore::NameHash, castl::vector<BufferHandle>> m_NameToBufferHandles;
		castl::unordered_map<cacore::NameHash, castl::vector<TextureSamplerDescriptor>> m_NameToSamplerDescriptors;
		castl::unordered_map<cacore::NameHash, castl::vector<castl::shared_ptr<VulkanShaderStruct>>> m_NameToSubStructs;
	};
}
