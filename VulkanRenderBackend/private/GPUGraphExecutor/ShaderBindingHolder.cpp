#include "../pch.h"
#include "ShaderBindingHolder.h"
#include "../VulkanApplication.h"

namespace graphics_backend
{
	struct DescritprorWriter
	{
		castl::vector<vk::WriteDescriptorSet> descriptorWrites;
		castl::vector<vk::DescriptorImageInfo> imageInfoList;
		castl::vector<vk::DescriptorBufferInfo> bufferInfoList;

		vk::DescriptorSet descriptorSet;

		void Initialize(vk::DescriptorSet set, uint32_t imageReserve, uint32_t samplerReserve, uint32_t constantBufferReserve, uint32_t bufferReserve)
		{
			descriptorSet = set;
			descriptorWrites.reserve(imageReserve + samplerReserve + bufferReserve + constantBufferReserve);
			imageInfoList.reserve(imageReserve + samplerReserve);
			bufferInfoList.reserve(bufferReserve + constantBufferReserve);
		}

		void AddBufferInfo(vk::Buffer buffer, uint32_t binding, vk::DescriptorType descriptorType)
		{
			bufferInfoList.push_back(vk::DescriptorBufferInfo(buffer, 0, VK_WHOLE_SIZE));
			descriptorWrites.push_back(vk::WriteDescriptorSet()
				.setDstSet(descriptorSet)
				.setDstBinding(binding)
				.setDstArrayElement(0)
				.setDescriptorType(descriptorType)
				.setDescriptorCount(1)
				.setPBufferInfo(&bufferInfoList.back()));
		}

		void AddWriteImageView(vk::ImageView imageView, uint32_t binding, vk::DescriptorType descriptorType, vk::ImageLayout layout)
		{
			imageInfoList.push_back(vk::DescriptorImageInfo({}, imageView, layout));
			descriptorWrites.push_back(vk::WriteDescriptorSet()
				.setDstSet(descriptorSet)
				.setDstBinding(binding)
				.setDstArrayElement(0)
				.setDescriptorType(descriptorType)
				.setDescriptorCount(1)
				.setPImageInfo(&imageInfoList.back()));
		}

		void AddWriteSampler(vk::Sampler sampler, uint32_t binding, vk::DescriptorType descriptorType)
		{
			imageInfoList.push_back(vk::DescriptorImageInfo(sampler, {}, {}));
			descriptorWrites.push_back(vk::WriteDescriptorSet()
				.setDstSet(descriptorSet)
				.setDstBinding(binding)
				.setDstArrayElement(0)
				.setDescriptorType(descriptorType)
				.setDescriptorCount(1)
				.setPImageInfo(&imageInfoList.back()));
		}

		void Apply(vk::Device device)
		{
			if (descriptorWrites.size() > 0)
			{
				device.updateDescriptorSets(descriptorWrites, {});
			}
		}
	};

	void ShaderBindingInstance::InitShaderBindings(CVulkanApplication& application, ShaderCompilerSlang::ShaderReflectionData const& reflectionData)
	{
		p_Application = &application;
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

	void WriteUniformBuffer(ShaderArgList const& shaderArgList
		, ShaderCompilerSlang::UniformBufferData const& bufferData
		, int32_t bufferGroupID
		, VulkanBufferHandle& dstBufferHandle)
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

	void WriteResources(ShaderArgList const& shaderArgList
		, ShaderCompilerSlang::ShaderBindingSpaceData const& bindingSpaceData
		, int32_t resourceGroupID
		, DescritprorWriter& writer)
	{
		auto& currentGroup = bindingSpaceData.m_ResourceGroups[resourceGroupID];
		for (uint32_t texID : currentGroup.m_Textures)
		{
			auto& textureData = bindingSpaceData.m_Textures[texID];
			auto imageHandles = shaderArgList.FindImageHandle(textureData.m_Name);
			//writer.AddWriteImageView()
		}
		for (uint32_t samplerID : currentGroup.m_Samplers)
		{
			auto& samplerData = bindingSpaceData.m_Samplers[samplerID];
			//writer.AddWriteSampler()
		}
		for (uint32_t bufferID : currentGroup.m_Buffers)
		{
			auto& bufferData = bindingSpaceData.m_Buffers[bufferID];
			auto bufferHandles = shaderArgList.FindBufferHandle(bufferData.m_Name);
			//writer.AddBufferInfo
		}

		for (uint32_t subGroupID : currentGroup.m_SubGroups)
		{
			auto found = shaderArgList.FindSubArgList(bindingSpaceData.m_ResourceGroups[subGroupID].m_Name);
			if (found)
			{
				WriteResources(*found, bindingSpaceData, subGroupID, writer);
			}
		}
	}

	void ShaderBindingInstance::WriteShaderData(CVulkanApplication& application, vk::CommandBuffer& command, ShaderArgList const& shaderArgList)
	{
		for (int sid = 0; sid < p_ReflectionData->m_BindingData.size(); ++sid)
		{
			DescritprorWriter writer;

			auto& targetDescSet = m_DescriptorSets[sid];
			auto& sourceSet = p_ReflectionData->m_BindingData[sid];

			//Uniform Buffers
			for (int ubid = 0; ubid < sourceSet.m_UniformBuffers.size(); ++ubid)
			{
				auto& sourceUniformBuffer = sourceSet.m_UniformBuffers[ubid];
				auto& bufferHandle = m_UniformBuffers[ubid];
				auto& group = sourceUniformBuffer.m_Groups[0];
				writer.AddBufferInfo(bufferHandle->GetBuffer(), sourceUniformBuffer.m_BindingIndex, vk::DescriptorType::eUniformBuffer);
				if (group.m_Name == "__Global")
				{
					vk::DeviceSize memorySize = sourceUniformBuffer.m_Groups[0].m_MemorySize;
					auto stageBuffer = p_Application->GetMemoryManager().AllocateBuffer(EMemoryType::GPU
						, EMemoryLifetime::FrameBound
						, memorySize
						, vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst);
					WriteUniformBuffer(shaderArgList, sourceUniformBuffer, 0, bufferHandle);
					command.copyBuffer(stageBuffer->GetBuffer(), bufferHandle->GetBuffer(), vk::BufferCopy(0, 0, memorySize));

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

			//Non Uniform Buffer Resources
			if (!sourceSet.m_ResourceGroups.empty())
			{
				WriteResources(shaderArgList, sourceSet, 0, writer);
			}
		}
	}

}

