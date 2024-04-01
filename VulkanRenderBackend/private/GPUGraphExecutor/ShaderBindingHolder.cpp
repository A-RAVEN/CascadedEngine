#include "../pch.h"
#include "ShaderBindingHolder.h"
#include "../VulkanApplication.h"

namespace graphics_backend
{
	void ShaderBindingInstance::InitShaderBindings(CVulkanApplication& application, ShaderCompilerSlang::ShaderReflectionData const& reflectionData)
	{
		p_ReflectionData = &reflectionData;
		m_DescriptorSets.resize(reflectionData.m_BindingData.size());
		for (int sid = 0; sid < reflectionData.m_BindingData.size(); ++sid)
		{
			auto& targetDescSet = m_DescriptorSets[sid];
			auto& sourceSet = reflectionData.m_BindingData[sid];
			ShaderDescriptorSetLayoutInfo layoutInfo;
			layoutInfo.m_StructuredBufferCount = sourceSet.m_Buffers.size();
			layoutInfo.m_TextureCount = sourceSet.m_Textures.size();
			layoutInfo.m_SamplerCount = sourceSet.m_Samplers.size();
			layoutInfo.m_ConstantBufferCount = sourceSet.m_UniformBuffers.size();
			targetDescSet = application.GetGPUObjectManager().GetShaderDescriptorPoolCache().GetOrCreate(layoutInfo)->AllocateSet();

			for(int ubid = 0; ubid < sourceSet.m_UniformBuffers.size(); ++ubid)
			{
				auto& sourceUniformBuffer = sourceSet.m_UniformBuffers[ubid];
				auto bufferHandle = application.GetMemoryManager().AllocateBuffer(EMemoryType::GPU
					, EMemoryLifetime::FrameBound
					, sourceUniformBuffer.m_Groups[0].m_MemorySize
					, vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst);
				m_UniformBuffers.push_back(bufferHandle);
			}
		}
	}

	void ShaderBindingInstance::WriteShaderData(ShaderArgList const& shaderArgList)
	{
		for (int sid = 0; sid < p_ReflectionData->m_BindingData.size(); ++sid)
		{
			auto& targetDescSet = m_DescriptorSets[sid];

			auto& sourceSet = p_ReflectionData->m_BindingData[sid];

			for (int ubid = 0; ubid < sourceSet.m_UniformBuffers.size(); ++ubid)
			{

				auto& sourceUniformBuffer = sourceSet.m_UniformBuffers[ubid];
				//auto bufferHandle = application.GetMemoryManager().AllocateBuffer(EMemoryType::GPU
				//	, EMemoryLifetime::FrameBound
				//	, sourceUniformBuffer.m_Groups[0].m_MemorySize
				//	, vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst);
				//m_UniformBuffers.push_back(bufferHandle);
			}
		}
	}

}

