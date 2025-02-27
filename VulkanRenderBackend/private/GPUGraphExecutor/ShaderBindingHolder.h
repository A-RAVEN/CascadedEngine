#pragma once
#include "ShaderArgList.h"
#include <Compiler.h>
#include <ResourcePool/FrameBoundResourcePool.h>
#include <GPUResources/GPUResourceInternal.h>

namespace graphics_backend
{
	class ShadderResourceProvider
	{
	public:
		virtual vk::Buffer GetBufferFromHandle(BufferHandle const& handle) = 0;
		virtual vk::ImageView GetImageView(ImageHandle const& handle, GPUTextureView const& view) = 0;
	};

	struct ShaderDescriptorSetInstance
	{
		struct ShaderUniformBufferBindings
		{
			uint32_t bindingID;
			uint32_t bufferStride;
			castl::vector<vk::Buffer> m_UniformBuffers;
		};

		vk::DescriptorSetLayout* p_Layout;
		vk::DescriptorSet* p_Set;

		void Init(vk::DescriptorSetLayout& descLayout, vk::DescriptorSet& descSet)
		{
			p_Layout = &descLayout;
			p_Set = &descSet;
		}
		cacore::HashObj<DescriptorSetDesc> m_DescriptorSetDesc;

		castl::vector<ShaderUniformBufferBindings> m_BoundUniformBuffers;
		ShaderUniformBufferBindings* GetUniformBufferBinding(uint32_t bindingID)
		{
			for (auto& binding : m_BoundUniformBuffers)
			{
				if (binding.bindingID == bindingID)
				{
					return &binding;
				}
			}
			return nullptr;
		}
	};

	class ShaderBindingInstance
	{
	public:
		void InitShaderBindingLayoutsNew(CVulkanApplication& application, ShaderCompilerSlang::ShaderReflectionData const& reflectionData, castl::string const& debugName);
		void InitShaderBindingSetsNew(FrameBoundResourcePool* pResourcePool);

		void FillShaderData(CVulkanApplication& application
			, ShadderResourceProvider& resourceProvider
			, FrameBoundResourcePool* pResourcePool
			, vk::CommandBuffer& command
			, castl::vector<castl::unordered_map<cacore::NameHash, castl::shared_ptr<ShaderStruct>> const*> const& shaderStructs);
		
		void PushUniformReadyBarriers(VulkanBarrierCollector& targetBarrierCollector, ResourceUsageFlags destUsage);

		uint32_t GetDescriptorSetCount() const { return static_cast<uint32_t>(m_DescriptorSetInstances.size()); }
		castl::vector<ShaderDescriptorSetInstance>const& GetDescriptorSetInstances() const { return m_DescriptorSetInstances; }

		ShaderCompilerSlang::ShaderReflectionData const* p_ReflectionData;
		CVulkanApplication* p_Application;

		//Descriptor Layouts And Sets
		castl::vector<ShaderDescriptorSetInstance> m_DescriptorSetInstances;
		castl::vector<vk::DescriptorSetLayout> m_DescriptorSetsLayouts;
		castl::vector<vk::DescriptorSet> m_DescriptorSets;

		//Collected Resources For Barrier Use
		castl::vector<castl::pair<BufferHandle, ShaderCompilerSlang::EShaderResourceAccess>> m_BufferHandles;
		castl::vector<castl::pair<ImageHandle, ShaderCompilerSlang::EShaderResourceAccess>> m_ImageHandles;

		//VulkanBarrierCollector m_PrepareShaderBindingsBarrierCollector;
	};


}