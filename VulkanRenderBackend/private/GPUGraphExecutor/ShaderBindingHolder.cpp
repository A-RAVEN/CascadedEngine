#include <pch.h>
#include <VulkanApplication.h>
#include <DescriptorAllocation/DescriptorLayoutPool.h>
#include "ShaderBindingHolder.h"

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

		void AddWriteBuffer(vk::Buffer buffer, uint32_t binding, vk::DescriptorType descriptorType, uint32_t arrayIndex)
		{
			bufferInfoList.push_back(vk::DescriptorBufferInfo(buffer, 0, VK_WHOLE_SIZE));
			descriptorWrites.push_back(vk::WriteDescriptorSet()
				.setDstSet(descriptorSet)
				.setDstBinding(binding)
				.setDstArrayElement(arrayIndex)
				.setDescriptorType(descriptorType)
				.setDescriptorCount(1)
				.setPBufferInfo(&bufferInfoList.back()));
		}

		void AddWriteImageView(vk::ImageView imageView, uint32_t binding, vk::DescriptorType descriptorType, vk::ImageLayout layout, uint32_t arrayIndex)
		{
			imageInfoList.push_back(vk::DescriptorImageInfo({}, imageView, layout));
			descriptorWrites.push_back(vk::WriteDescriptorSet()
				.setDstSet(descriptorSet)
				.setDstBinding(binding)
				.setDstArrayElement(arrayIndex)
				.setDescriptorType(descriptorType)
				.setDescriptorCount(1)
				.setPImageInfo(&imageInfoList.back()));
		}

		void AddWriteSampler(vk::Sampler sampler, uint32_t binding)
		{
			imageInfoList.push_back(vk::DescriptorImageInfo(sampler, {}, {}));
			descriptorWrites.push_back(vk::WriteDescriptorSet()
				.setDstSet(descriptorSet)
				.setDstBinding(binding)
				.setDstArrayElement(0)
				.setDescriptorType(vk::DescriptorType::eSampler)
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

	void ShaderBindingInstance::InitShaderBindings(CVulkanApplication& application, FrameBoundResourcePool* pResourcePool, ShaderCompilerSlang::ShaderReflectionData const& reflectionData)
	{
		p_Application = &application;
		p_ReflectionData = &reflectionData;
		m_DescriptorSets.resize(reflectionData.m_BindingData.size());
		m_DescriptorSetsLayouts.resize(reflectionData.m_BindingData.size());
		for (int sid = 0; sid < reflectionData.m_BindingData.size(); ++sid)
		{
			auto& targetDescSet = m_DescriptorSets[sid];
			auto& sourceSet = reflectionData.m_BindingData[sid];
			uint32_t bindingCount = sourceSet.GetBindingCount();

			DescriptorSetDesc descSetDesc;
			descSetDesc.descs.reserve(bindingCount);
			for (auto& uniformBuf : sourceSet.m_UniformBuffers)
			{
				DescriptorDesc bindingDesc;
				bindingDesc.bindingIndex = uniformBuf.m_BindingIndex;
				bindingDesc.arraySize = 1;
				bindingDesc.descType = vk::DescriptorType::eUniformBuffer;
				descSetDesc.descs.push_back(bindingDesc);
			}
			for (auto& textureBinding : sourceSet.m_Textures)
			{
				DescriptorDesc bindingDesc;
				bindingDesc.bindingIndex = textureBinding.m_BindingIndex;
				bindingDesc.arraySize = textureBinding.m_Count;
				bindingDesc.descType = vk::DescriptorType::eSampledImage;
				descSetDesc.descs.push_back(bindingDesc);
			}
			for (auto& samplerBinding : sourceSet.m_Samplers)
			{
				DescriptorDesc bindingDesc;
				bindingDesc.bindingIndex = samplerBinding.m_BindingIndex;
				bindingDesc.arraySize = samplerBinding.m_Count;
				bindingDesc.descType = vk::DescriptorType::eSampler;
				descSetDesc.descs.push_back(bindingDesc);
			}
			for (auto& storageBufferBinding : sourceSet.m_Buffers)
			{
				DescriptorDesc bindingDesc;
				bindingDesc.bindingIndex = storageBufferBinding.m_BindingIndex;
				bindingDesc.arraySize = storageBufferBinding.m_Count;
				bindingDesc.descType = vk::DescriptorType::eStorageBuffer;
				descSetDesc.descs.push_back(bindingDesc);
			}
			auto descSetLayout = application.GetGPUObjectManager().m_DescriptorSetAllocatorDic.GetOrCreate(descSetDesc);
			auto descSetAllocator = pResourcePool->descriptorPools.GetOrCreate(descSetDesc.GetPoolDesc());

			m_DescriptorSetsLayouts[sid] = descSetLayout->GetLayout();
			targetDescSet = descSetAllocator->AllocateSet(descSetLayout->GetLayout());

			for(int ubid = 0; ubid < sourceSet.m_UniformBuffers.size(); ++ubid)
			{
				auto& sourceUniformBuffer = sourceSet.m_UniformBuffers[ubid];
				auto bufferObject = pResourcePool->CreateBufferWithMemory(GPUBufferDescriptor::Create(
					EBufferUsage::eConstantBuffer | EBufferUsage::eDataDst
					, 1, sourceUniformBuffer.m_Groups[0].m_MemorySize), vk::MemoryPropertyFlagBits::eDeviceLocal);
				m_UniformBuffers.push_back(castl::move(bufferObject));
			}
		}
	}

	void WriteUniformBuffer(ShaderArgList const& shaderArgList
		, ShaderCompilerSlang::UniformBufferData const& bufferData
		, int32_t bufferGroupID
		, char* dataDst)
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
					memcpy(dataDst + elementOffset, pDataPos, element.m_ElementMemorySize);
				}
			}
		}
		for(uint32_t subGroupID : group.m_SubGroups)
		{
			auto found = shaderArgList.FindSubArgList(bufferData.m_Groups[subGroupID].m_Name);
			if(found)
			{
				WriteUniformBuffer(*found, bufferData, subGroupID, dataDst);
			}
		}
	}

	void WriteResources(CVulkanApplication& application
		, ShaderArgList const& shaderArgList
		, ShadderResourceProvider& resourceProvider
		, ShaderCompilerSlang::ShaderBindingSpaceData const& bindingSpaceData
		, int32_t resourceGroupID
		, DescritprorWriter& writer
		, castl::vector<castl::pair<BufferHandle, ShaderCompilerSlang::EShaderResourceAccess>>& inoutBufferHandles
		, castl::vector<castl::pair<ImageHandle, ShaderCompilerSlang::EShaderResourceAccess>>& inoutImageHandles)
	{
		auto& currentGroup = bindingSpaceData.m_ResourceGroups[resourceGroupID];
		for (uint32_t texID : currentGroup.m_Textures)
		{
			auto& textureData = bindingSpaceData.m_Textures[texID];
			auto imageHandles = shaderArgList.FindImageHandle(textureData.m_Name);
			for (uint32_t imgID = 0; imgID < imageHandles.size(); ++imgID)
			{
				inoutImageHandles.push_back(castl::make_pair( imageHandles[imgID].first, textureData.m_Access ));
				auto& imagePair = imageHandles[imgID];
				vk::ImageView imageView = resourceProvider.GetImageView(imagePair.first, imagePair.second);

				vk::ImageLayout targetLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
				vk::DescriptorType descriptorType = vk::DescriptorType::eSampledImage;
				switch (textureData.m_Access)
				{
					case ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly:
					case ShaderCompilerSlang::EShaderResourceAccess::eReadWrite:
						targetLayout = vk::ImageLayout::eGeneral;
						descriptorType = vk::DescriptorType::eStorageImage;
						break;
				}
				writer.AddWriteImageView(imageView
					, textureData.m_BindingIndex
					, descriptorType
					//TODO: Readonly Type?
					, targetLayout
					, imgID);
			}
		}
		for (uint32_t samplerID : currentGroup.m_Samplers)
		{
			auto& samplerData = bindingSpaceData.m_Samplers[samplerID];
			auto samplerDescs = shaderArgList.FindSampler(samplerData.m_Name);
			auto sampler = application.GetGPUObjectManager().GetTextureSamplerCache().GetOrCreate(samplerDescs);
			writer.AddWriteSampler(sampler->GetSampler(), samplerData.m_BindingIndex);
		}
		for (uint32_t bufferID : currentGroup.m_Buffers)
		{
			auto& bufferData = bindingSpaceData.m_Buffers[bufferID];
			auto bufferHandles = shaderArgList.FindBufferHandle(bufferData.m_Name);
			for (uint32_t bufID = 0; bufID < bufferHandles.size(); ++bufID)
			{
				inoutBufferHandles.push_back(castl::make_pair(bufferHandles[bufID], bufferData.m_Access));
				vk::Buffer buffer = resourceProvider.GetBufferFromHandle(bufferHandles[bufID]);
				writer.AddWriteBuffer(buffer, bufferData.m_BindingIndex, vk::DescriptorType::eStorageBuffer, bufID);
			}
		}

		for (uint32_t subGroupID : currentGroup.m_SubGroups)
		{
			auto found = shaderArgList.FindSubArgList(bindingSpaceData.m_ResourceGroups[subGroupID].m_Name);
			if (found)
			{
				WriteResources(application
					, *found
					, resourceProvider
					, bindingSpaceData
					, subGroupID
					, writer
					, inoutBufferHandles
					, inoutImageHandles);
			}
		}
	}

	void ShaderBindingInstance::WriteShaderData(CVulkanApplication& application
		, ShadderResourceProvider& resourceProvider
		, FrameBoundResourcePool* pResourcePool
		, vk::CommandBuffer& command
		, ShaderArgList const& shaderArgList)
	{
		for (int sid = 0; sid < p_ReflectionData->m_BindingData.size(); ++sid)
		{
			DescritprorWriter writer;

			auto& targetDescSet = m_DescriptorSets[sid];
			auto& sourceSet = p_ReflectionData->m_BindingData[sid];

			writer.Initialize(targetDescSet, sourceSet.m_Textures.size(), sourceSet.m_Samplers.size(), sourceSet.m_UniformBuffers.size(), sourceSet.m_Buffers.size());

			//Uniform Buffers
			for (int ubid = 0; ubid < sourceSet.m_UniformBuffers.size(); ++ubid)
			{
				auto& sourceUniformBuffer = sourceSet.m_UniformBuffers[ubid];
				auto& bufferHandle = m_UniformBuffers[ubid];
				auto& group = sourceUniformBuffer.m_Groups[0];
				writer.AddWriteBuffer(bufferHandle.buffer, sourceUniformBuffer.m_BindingIndex, vk::DescriptorType::eUniformBuffer, 0);
				vk::DeviceSize memorySize = group.m_MemorySize;
				auto stageBuffer = pResourcePool->CreateStagingBuffer(memorySize, EBufferUsage::eDataSrc);
				if (group.m_Name == "__Global")
				{
					auto tmpMap = pResourcePool->memoryManager.ScopedMapMemory(stageBuffer.allocation);
					WriteUniformBuffer(shaderArgList, sourceUniformBuffer, 0, static_cast<char*>(tmpMap.mappedMemory));
				}
				else
				{
					auto found = shaderArgList.FindSubArgList(group.m_Name);
					if (found)
					{
						auto tmpMap = pResourcePool->memoryManager.ScopedMapMemory(stageBuffer.allocation);
						WriteUniformBuffer(*found, sourceUniformBuffer, 0, static_cast<char*>(tmpMap.mappedMemory));
					}
				}
				command.copyBuffer(stageBuffer.buffer, bufferHandle.buffer, vk::BufferCopy(0, 0, memorySize));
			}

			//Non Uniform Buffer Resources
			if (!sourceSet.m_ResourceGroups.empty())
			{
				if (sourceSet.m_ResourceGroups[0].m_Name != "__Global")
				{
					auto found = shaderArgList.FindSubArgList(sourceSet.m_ResourceGroups[0].m_Name);
					if (found)
					{
						WriteResources(
							application
							, *found
							, resourceProvider
							, sourceSet
							, 0
							, writer
							, m_BufferHandles
							, m_ImageHandles);
					}
				}
				else
				{
					WriteResources(
						application
						, shaderArgList
						, resourceProvider
						, sourceSet
						, 0
						, writer
						, m_BufferHandles
						, m_ImageHandles);
				}
			}

			writer.Apply(application.GetDevice());
		}
	}

	void ShaderBindingInstance::FillShaderData(CVulkanApplication& application
		, ShadderResourceProvider& resourceProvider
		, FrameBoundResourcePool* pResourcePool
		, vk::CommandBuffer& command
		, castl::vector<castl::shared_ptr<ShaderArgList>> const& shaderArgLists)
	{
		for (int sid = 0; sid < p_ReflectionData->m_BindingData.size(); ++sid)
		{
			DescritprorWriter writer;

			auto& targetDescSet = m_DescriptorSets[sid];
			auto& sourceSet = p_ReflectionData->m_BindingData[sid];

			writer.Initialize(targetDescSet, sourceSet.m_Textures.size(), sourceSet.m_Samplers.size(), sourceSet.m_UniformBuffers.size(), sourceSet.m_Buffers.size());
		
			//Uniform Buffers
			for (int ubid = 0; ubid < sourceSet.m_UniformBuffers.size(); ++ubid)
			{
				auto& sourceUniformBuffer = sourceSet.m_UniformBuffers[ubid];
				auto& bufferHandle = m_UniformBuffers[ubid];
				auto& group = sourceUniformBuffer.m_Groups[0];
				writer.AddWriteBuffer(bufferHandle.buffer, sourceUniformBuffer.m_BindingIndex, vk::DescriptorType::eUniformBuffer, 0);
				vk::DeviceSize memorySize = group.m_MemorySize;
				auto stageBuffer = pResourcePool->CreateStagingBuffer(memorySize, EBufferUsage::eDataSrc);
				if (group.m_Name == "__Global")
				{
					//For Global Uniform Constants We Collect Data From All ShaderArgLists
					auto tmpMap = pResourcePool->memoryManager.ScopedMapMemory(stageBuffer.allocation);
					for(auto shaderArgList = shaderArgLists.crbegin(); shaderArgList != shaderArgLists.crend(); ++shaderArgList)
					{
						WriteUniformBuffer(**shaderArgList, sourceUniformBuffer, 0, static_cast<char*>(tmpMap.mappedMemory));
					}
				}
				else
				{
					for (auto shaderArgList = shaderArgLists.crbegin(); shaderArgList != shaderArgLists.crend(); ++shaderArgList)
					{
						auto found = shaderArgList->get()->FindSubArgList(group.m_Name);
						if (found)
						{
							auto tmpMap = pResourcePool->memoryManager.ScopedMapMemory(stageBuffer.allocation);
							WriteUniformBuffer(*found, sourceUniformBuffer, 0, static_cast<char*>(tmpMap.mappedMemory));
						}
					}
				}
				command.copyBuffer(stageBuffer.buffer, bufferHandle.buffer, vk::BufferCopy(0, 0, memorySize));
			}


			//Non Uniform Buffer Resources
			if (!sourceSet.m_ResourceGroups.empty())
			{
				if (sourceSet.m_ResourceGroups[0].m_Name != "__Global")
				{
					for (auto shaderArgList = shaderArgLists.crbegin(); shaderArgList != shaderArgLists.crend(); ++shaderArgList)
					{
						auto found = shaderArgList->get()->FindSubArgList(sourceSet.m_ResourceGroups[0].m_Name);
						if (found)
						{
							WriteResources(
								application
								, *found
								, resourceProvider
								, sourceSet
								, 0
								, writer
								, m_BufferHandles
								, m_ImageHandles);
						}
					}
				}
				else
				{
					for (auto shaderArgList = shaderArgLists.crbegin(); shaderArgList != shaderArgLists.crend(); ++shaderArgList)
					{
						WriteResources(
							application
							, **shaderArgList
							, resourceProvider
							, sourceSet
							, 0
							, writer
							, m_BufferHandles
							, m_ImageHandles);
					}
				}
			}
			writer.Apply(application.GetDevice());
		}
	}

}

