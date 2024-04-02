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

	void WriteUniformBuffer(ShaderArgList const& shaderArgList, ShaderCompilerSlang::UniformBufferData const& bufferData, int32_t bufferGroupID, VulkanBufferHandle& dstBufferHandle)
	{
		auto& group = bufferData.m_Groups[bufferGroupID];
		for (auto& element : group.m_Elements)
		{
			if (auto pDataPos = shaderArgList.FindNumericDataPointer(element.m_Name))
			{
				uint32_t memoryOffset = element.m_MemoryOffset;
				for (uint32_t elementID = 0; elementID < element.m_ElementCount; ++elementID)
				{
					uint32_t elementOffset = memoryOffset + element.m_Stride * elementID;
					memcpy(dstBufferHandle->GetMappedPointer(), pDataPos, element.m_ElementMemorySize);
				}
			}
		}
		for(uint32_t subGroupID : group.m_SubGroups)
		{
			auto found = shaderArgList.FindSubArgList(bufferData.m_Groups[subGroupID].m_Name);
			if(found)
			{
				WriteUniformBuffer(*found, bufferData, subGroupID, dstBufferHandle);
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
				auto& bufferHandle = m_UniformBuffers[ubid];
				auto& group = sourceUniformBuffer.m_Groups[0];
				if (group.m_Name == "__Global")
				{
					WriteUniformBuffer(shaderArgList, sourceUniformBuffer, 0, bufferHandle);
				}
				else
				{
					auto found = shaderArgList.FindSubArgList(group.m_Name);
					if (found)
					{
						WriteUniformBuffer(*found, sourceUniformBuffer, 0, bufferHandle);
					}
				}
			}
		}
	}

}

