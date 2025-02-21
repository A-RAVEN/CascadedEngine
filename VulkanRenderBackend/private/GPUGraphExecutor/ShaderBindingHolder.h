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
		vk::DescriptorSetLayout m_Layout;
		cacore::HashObj<DescriptorSetDesc> m_DescriptorSetDesc;
		vk::DescriptorSet m_Set;
		struct ShaderUniformBufferBindings
		{
			uint32_t bindingID;
			uint32_t bufferStride;
			castl::vector<VKBufferObject> m_UniformBuffers;
		};
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
		//void InitShaderBindingLayouts(CVulkanApplication& application, ShaderCompilerSlang::ShaderReflectionData const& reflectionData, castl::string const& debugName);
		void InitShaderBindingLayoutsNew(CVulkanApplication& application, ShaderCompilerSlang::ShaderReflectionData const& reflectionData, castl::string const& debugName);
		void InitShaderBindingSets(FrameBoundResourcePool* pResourcePool);
		void InitShaderBindingSetsNew(FrameBoundResourcePool* pResourcePool);
		void InitShaderBindings(CVulkanApplication& application, FrameBoundResourcePool* pResourcePool, ShaderCompilerSlang::ShaderReflectionData const& reflectionData);
		void FillShaderData(CVulkanApplication& application
			, ShadderResourceProvider& resourceProvider
			, FrameBoundResourcePool* pResourcePool
			, vk::CommandBuffer& command
			, castl::vector <castl::pair <castl::string, castl::shared_ptr<ShaderArgList>>> const& shaderArgLists);
		void FillShaderData(CVulkanApplication& application
			, ShadderResourceProvider& resourceProvider
			, FrameBoundResourcePool* pResourcePool
			, vk::CommandBuffer& command
			, castl::vector<castl::unordered_map<cacore::NameHash, castl::shared_ptr<ShaderStruct>> const*> const& shaderStructs);
		
		castl::vector<ShaderDescriptorSetInstance> m_DescriptorSetInstances;

		castl::vector<vk::DescriptorSetLayout> m_DescriptorSetsLayouts;
		castl::vector<cacore::HashObj<DescriptorSetDesc>> m_DescriptorSetDescs;

		castl::vector<vk::DescriptorSet> m_DescriptorSets;
		castl::map<uint32_t, castl::vector<VKBufferObject>> m_UniformBuffers;
		ShaderCompilerSlang::ShaderReflectionData const* p_ReflectionData;
		CVulkanApplication* p_Application;

		//Collected Resources For Barrier Use
		castl::vector<castl::pair<BufferHandle, ShaderCompilerSlang::EShaderResourceAccess>> m_BufferHandles;
		castl::vector<castl::pair<ImageHandle, ShaderCompilerSlang::EShaderResourceAccess>> m_ImageHandles;
	};


}