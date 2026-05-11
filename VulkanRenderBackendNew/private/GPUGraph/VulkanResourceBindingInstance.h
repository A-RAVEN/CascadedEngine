#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <ShaderStruct.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAVector.h>
#include <ShaderLibrary/ShaderLibrary.h>
#include <Common.h>

namespace graphics_backend
{
	class VulkanShaderStruct;
	class VulkanShaderResourceSet;
	class VulkanGraphLocalResourceManager;
	class VulkanConstantBufferManager;
	class VulkanGraphExecutor;
	class GPUGraph;

	// Binding element types (references D3D12 GPUResourceBindingInstance pattern)
	struct CBufferBindingElement
	{
		uint32_t offset;
		VulkanCBufferBindingInfo bindingInfo;
		VulkanShaderStruct const* pCBufferStruct = nullptr;
		uint64_t gpuBufferResourceId = 0;
		vk::ShaderStageFlags usingStages;
	};

	struct ImageBindingElement
	{
		uint32_t offset;
		VulkanImageBindingInfo bindingInfo;
		castl::vector<castl::pair<ImageHandle, GPUTextureView>> bindings;
		vk::ShaderStageFlags usingStages;
		EResourceUsageFlags resourceUsages;
	};

	struct BufferBindingElement
	{
		uint32_t offset;
		VulkanBufferBindingInfo bindingInfo;
		castl::vector<BufferHandle> bindings;
		vk::ShaderStageFlags usingStages;
		EResourceUsageFlags resourceUsages;
	};

	struct SamplerBindingElement
	{
		uint32_t offset;
		VulkanSamplerBindingInfo bindingInfo;
		castl::vector<TextureSamplerDescriptor> samplerDescriptors;
		vk::ShaderStageFlags usingStages;
	};

	// Shader resource binding for descriptor sets
	class VulkanResourceBindingInstance : public VulkanSubobjectBase
	{
	public:
		VulkanResourceBindingInstance() = default;
		~VulkanResourceBindingInstance() = default;

		// Initialize from shader resource set (references D3D12 GPUResourceBindingInstance::Init)
		void Init(VulkanShaderResourceSet const& resourceSet);
		virtual void Release() override;

		// Build resources into local resource manager
		void BuildResources(VulkanGraphLocalResourceManager& resourceManager, VulkanConstantBufferManager& cbufferManager, GPUGraph const& graph);

		// Build descriptors - allocate and update descriptor sets
		void BuildDescriptors(VulkanGraphLocalResourceManager& resourceManager, vk::DescriptorPool pool);

		// Set resources (low-level API for manual descriptor writes)
		void SetUniformBuffer(uint32_t set, uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range);
		void SetStorageBuffer(uint32_t set, uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range);
		void SetSampledImage(uint32_t set, uint32_t binding, vk::ImageView imageView, vk::ImageLayout layout);
		void SetStorageImage(uint32_t set, uint32_t binding, vk::ImageView imageView, vk::ImageLayout layout);
		void SetSampler(uint32_t set, uint32_t binding, vk::Sampler sampler);

		// Get descriptor set for a set index
		vk::DescriptorSet GetDescriptorSet(uint32_t set) const;

		// Get all allocated descriptor sets sorted by set index
		castl::vector<castl::pair<uint32_t, vk::DescriptorSet>> GetDescriptorSetsSorted() const;

		// Allocate descriptor sets from pool (pairs: setIndex -> layout)
		bool AllocateDescriptorSets(vk::DescriptorPool pool, castl::vector<castl::pair<uint32_t, vk::DescriptorSetLayout>> const& setLayoutPairs);

		// Update descriptor sets on GPU
		void UpdateDescriptorSets();

		// Get descriptor writes for batch update
		castl::vector<vk::WriteDescriptorSet> const& GetPendingWrites() const { return m_PendingWrites; }
		void ClearPendingWrites() { m_PendingWrites.clear(); }

		// Access binding elements
		castl::vector<CBufferBindingElement> const& GetCBufferBindings() const { return m_CBufferBindings; }
		castl::vector<ImageBindingElement> const& GetImageBindings() const { return m_ImageBindings; }
		castl::vector<BufferBindingElement> const& GetBufferBindings() const { return m_BufferBindings; }
		castl::vector<SamplerBindingElement> const& GetSamplerBindings() const { return m_SamplerBindings; }

		// Access shader file info
		VulkanShaderFileInfo const* GetShaderFileInfo() const { return p_ShaderFileInfo; }

	private:
		// Binding elements populated from reflection
		castl::vector<CBufferBindingElement> m_CBufferBindings;
		castl::vector<ImageBindingElement> m_ImageBindings;
		castl::vector<BufferBindingElement> m_BufferBindings;
		castl::vector<SamplerBindingElement> m_SamplerBindings;

		// Shader file info used for reflection
		VulkanShaderFileInfo const* p_ShaderFileInfo = nullptr;

		// Descriptor sets per set index
		castl::unordered_map<uint32_t, vk::DescriptorSet> m_DescriptorSets;

		// Pending descriptor writes
		castl::vector<vk::WriteDescriptorSet> m_PendingWrites;

		// Storage for descriptor info (must persist until update)
		castl::vector<vk::DescriptorBufferInfo> m_BufferInfos;
		castl::vector<vk::DescriptorImageInfo> m_ImageInfos;
	
	};
}
