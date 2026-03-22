#include <pch.h>
#include <VulkanApplication.h>
#include <DescriptorAllocation/DescriptorLayoutPool.h>
#include "ShaderBindingHolder.h"
#include <ShaderStruct/VKShaderStruct.h>
#include <DebugUtils.h>

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

		void AddWriteBuffers(castl::array_ref<vk::Buffer> buffers
			, uint32_t binding, vk::DescriptorType descriptorType, uint32_t arrayIndex, uint32_t count)
		{
			if (count == 0)
				return;
			CA_ASSERT_BREAK(bufferInfoList.size() + count <= bufferInfoList.capacity(), "Not Enough Reserved Space For Buffer Writes");
			CA_ASSERT_BREAK(buffers.size() >= count, "Not Big Enough Buffer Array");

			uint32_t start = bufferInfoList.size();

			for (int i = 0; i < count; ++i)
			{
				bufferInfoList.push_back(vk::DescriptorBufferInfo(buffers.data()[i], 0, VK_WHOLE_SIZE));
			}
			descriptorWrites.push_back(vk::WriteDescriptorSet()
				.setDstSet(descriptorSet)
				.setDstBinding(binding)
				.setDstArrayElement(arrayIndex)
				.setDescriptorType(descriptorType)
				.setDescriptorCount(count)
				.setPBufferInfo(&bufferInfoList[start]));
		}

		void AddWriteImageViews(castl::array_ref<vk::ImageView>imageViews
			, uint32_t binding, vk::DescriptorType descriptorType, vk::ImageLayout layout, uint32_t arrayIndex, uint32_t count)
		{
			if (count == 0)
				return;
			CA_ASSERT_BREAK(imageInfoList.size() + count <= imageInfoList.capacity(), "Not Enough Reserved Space For ImageView Writes");
			CA_ASSERT_BREAK(imageViews.size() >= count, "Not Big Enough ImageView Array");

			uint32_t start = imageInfoList.size();

			for (int i = 0; i < count; ++i)
			{
				imageInfoList.push_back(vk::DescriptorImageInfo({}, imageViews.data()[i], layout));
			}
			descriptorWrites.push_back(vk::WriteDescriptorSet()
				.setDstSet(descriptorSet)
				.setDstBinding(binding)
				.setDstArrayElement(arrayIndex)
				.setDescriptorType(descriptorType)
				.setDescriptorCount(count)
				.setPImageInfo(&imageInfoList.back()));
		}

		void AddWriteBuffer(vk::Buffer buffer, uint32_t binding, vk::DescriptorType descriptorType, uint32_t arrayIndex)
		{
			CA_ASSERT_BREAK(bufferInfoList.size() < bufferInfoList.capacity(), "Not Enough Reserved Space For Buffer Write");

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
			CA_ASSERT_BREAK(imageInfoList.size() < imageInfoList.capacity(), "Not Enough Reserved Space For ImageView Write");
			imageInfoList.push_back(vk::DescriptorImageInfo({}, imageView, layout));
			descriptorWrites.push_back(vk::WriteDescriptorSet()
				.setDstSet(descriptorSet)
				.setDstBinding(binding)
				.setDstArrayElement(arrayIndex)
				.setDescriptorType(descriptorType)
				.setDescriptorCount(1)
				.setPImageInfo(&imageInfoList.back()));
		}

		void AddWriteSampler(vk::Sampler sampler, uint32_t binding, uint32_t arrayIndex = 0)
		{
			CA_ASSERT_BREAK(imageInfoList.size() < imageInfoList.capacity(), "Not Enough Reserved Space For Sampler Write");
			imageInfoList.push_back(vk::DescriptorImageInfo(sampler, {}, {}));
			descriptorWrites.push_back(vk::WriteDescriptorSet()
				.setDstSet(descriptorSet)
				.setDstBinding(binding)
				.setDstArrayElement(arrayIndex)
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

	//void ShaderBindingInstance::InitShaderBindingLayouts(CVulkanApplication& application
	//	, ShaderCompilerSlang::ShaderReflectionData const& reflectionData
	//	, castl::string const& debugName)
	//{
	//	castl::string debug = "binding For Shader " + debugName + ": ";
	//	p_Application = &application;
	//	p_ReflectionData = &reflectionData;
	//	m_DescriptorSetsLayouts.resize(reflectionData.m_BindingData.size());
	//	m_DescriptorSetDescs.resize(reflectionData.m_BindingData.size());
	//	for (int sid = 0; sid < reflectionData.m_BindingData.size(); ++sid)
	//	{
	//		auto& sourceSet = reflectionData.m_BindingData[sid];
	//		uint32_t bindingCount = sourceSet.GetBindingCount();
	//		DescriptorSetDesc descSetDesc;
	//		descSetDesc.descs.reserve(bindingCount);
	//		for (auto& uniformBuf : sourceSet.m_UniformBuffers)
	//		{
	//			DescriptorDesc bindingDesc;
	//			bindingDesc.bindingIndex = uniformBuf.m_BindingIndex;
	//			bindingDesc.arraySize = 1;
	//			bindingDesc.descType = vk::DescriptorType::eUniformBuffer;
	//			descSetDesc.descs.push_back(bindingDesc);
	//			debug += "Uniform Buffer Space:" + castl::to_string(sid) + " Binding:" + castl::to_string(uniformBuf.m_BindingIndex) + " ";
	//		}
	//		for (auto& textureBinding : sourceSet.m_Textures)
	//		{
	//			DescriptorDesc bindingDesc;
	//			bindingDesc.bindingIndex = textureBinding.m_BindingIndex;
	//			bindingDesc.arraySize = textureBinding.m_Count;
	//			bindingDesc.descType = vk::DescriptorType::eSampledImage;
	//			descSetDesc.descs.push_back(bindingDesc);
	//			debug += "Texture Space:" + castl::to_string(sid) + " Binding:" + castl::to_string(textureBinding.m_BindingIndex) + " ";
	//		}
	//		for (auto& samplerBinding : sourceSet.m_Samplers)
	//		{
	//			DescriptorDesc bindingDesc;
	//			bindingDesc.bindingIndex = samplerBinding.m_BindingIndex;
	//			bindingDesc.arraySize = samplerBinding.m_Count;
	//			bindingDesc.descType = vk::DescriptorType::eSampler;
	//			descSetDesc.descs.push_back(bindingDesc);
	//			debug += "Sampler Space:" + castl::to_string(sid) + " Binding:" + castl::to_string(samplerBinding.m_BindingIndex) + " ";
	//		}
	//		for (auto& storageBufferBinding : sourceSet.m_Buffers)
	//		{
	//			DescriptorDesc bindingDesc;
	//			bindingDesc.bindingIndex = storageBufferBinding.m_BindingIndex;
	//			bindingDesc.arraySize = storageBufferBinding.m_Count;
	//			bindingDesc.descType = vk::DescriptorType::eStorageBuffer;
	//			descSetDesc.descs.push_back(bindingDesc);
	//			debug += "Storage Buffer Space:" + castl::to_string(sid) + " Binding:" + castl::to_string(storageBufferBinding.m_BindingIndex) + " ";
	//		}
	//		auto descSetLayout = application.GetGPUObjectManager().GetDescriptorSetLayoutCache().GetOrCreate(descSetDesc);
	//		m_DescriptorSetDescs[sid] = descSetDesc;
	//		m_DescriptorSetsLayouts[sid] = descSetLayout->GetLayout();
	//	}
	//	//castl::cout << debug << castl::endl;
	//}

	void ShaderBindingInstance::InitShaderBindingLayoutsNew(CVulkanApplication& application, ShaderCompilerSlang::ShaderReflectionData const& reflectionData, castl::string const& debugName)
	{
		p_Application = &application;
		p_ReflectionData = &reflectionData;

		

		auto& spaceInfos = p_ReflectionData->m_BindingInfo.m_SpaceInfos;
		auto& hierarcies = p_ReflectionData->m_BindingInfo.m_BindingDataHierarchies;
		m_DescriptorSetInstances.resize(spaceInfos.size());
		m_DescriptorSetsLayouts.resize(spaceInfos.size());
		m_DescriptorSets.resize(spaceInfos.size());

		for (size_t spaceID = 0; spaceID < spaceInfos.size(); ++spaceID)
		{
			auto& descSetInst = m_DescriptorSetInstances[spaceID];
			descSetInst.Init(m_DescriptorSetsLayouts[spaceID], m_DescriptorSets[spaceID]);
			auto& spaceInfo = spaceInfos[spaceID];
			uint32_t bindingCount = spaceInfo.m_ResourceStats.m_CBufferBindings.size()
				+ spaceInfo.m_ResourceStats.m_TextureBindings.size()
				+ spaceInfo.m_ResourceStats.m_RWTextureBindings.size()
				+ spaceInfo.m_ResourceStats.m_SamplerBindings.size()
				+ spaceInfo.m_ResourceStats.m_StorageBufferBindings.size()
				+ spaceInfo.m_ResourceStats.m_RWBufferBindings.size();

			DescriptorSetDesc descSetDesc;
			descSetDesc.descs.reserve(bindingCount);
			for (auto& desc : spaceInfo.m_ResourceStats.m_CBufferBindings)
			{
				DescriptorDesc bindingDesc;
				bindingDesc.bindingIndex = desc.bindingID;
				bindingDesc.arraySize = desc.elementCount;
				bindingDesc.descType = vk::DescriptorType::eUniformBuffer;
				descSetDesc.descs.push_back(bindingDesc);
			}
			for (auto& desc : spaceInfo.m_ResourceStats.m_TextureBindings)
			{
				DescriptorDesc bindingDesc;
				bindingDesc.bindingIndex = desc.bindingID;
				bindingDesc.arraySize = desc.elementCount;
				bindingDesc.descType = vk::DescriptorType::eSampledImage;
				descSetDesc.descs.push_back(bindingDesc);
			}
			for (auto& desc : spaceInfo.m_ResourceStats.m_RWTextureBindings)
			{
				DescriptorDesc bindingDesc;
				bindingDesc.bindingIndex = desc.bindingID;
				bindingDesc.arraySize = desc.elementCount;
				bindingDesc.descType = vk::DescriptorType::eStorageImage;
				descSetDesc.descs.push_back(bindingDesc);
			}
			for (auto& desc : spaceInfo.m_ResourceStats.m_SamplerBindings)
			{
				DescriptorDesc bindingDesc;
				bindingDesc.bindingIndex = desc.bindingID;
				bindingDesc.arraySize = desc.elementCount;
				bindingDesc.descType = vk::DescriptorType::eSampler;
				descSetDesc.descs.push_back(bindingDesc);
			}
			for (auto& desc : spaceInfo.m_ResourceStats.m_StorageBufferBindings)
			{
				DescriptorDesc bindingDesc;
				bindingDesc.bindingIndex = desc.bindingID;
				bindingDesc.arraySize = desc.elementCount;
				bindingDesc.descType = vk::DescriptorType::eStorageBuffer;
				descSetDesc.descs.push_back(bindingDesc);
			}
			for (auto& desc : spaceInfo.m_ResourceStats.m_RWBufferBindings)
			{
				DescriptorDesc bindingDesc;
				bindingDesc.bindingIndex = desc.bindingID;
				bindingDesc.arraySize = desc.elementCount;
				bindingDesc.descType = vk::DescriptorType::eStorageBuffer;
				descSetDesc.descs.push_back(bindingDesc);
			}
			auto descSetLayout = application.GetGPUObjectManager().GetDescriptorSetLayoutCache().GetOrCreate(descSetDesc);
			descSetInst.m_DescriptorSetDesc = descSetDesc;
			*descSetInst.p_Layout = descSetLayout->GetLayout();
		}
	}

	void ShaderBindingInstance::InitShaderBindingSetsNew(FrameBoundResourcePool* pResourcePool)
	{
		auto& spaceInfos = p_ReflectionData->m_BindingInfo.m_SpaceInfos;
		auto& hierarcies = p_ReflectionData->m_BindingInfo.m_BindingDataHierarchies;
		auto descriptorPool = pResourcePool->descriptorPools.AquirePool();
		for (size_t spaceID = 0; spaceID < spaceInfos.size(); ++spaceID)
		{
			auto& descSetInst = m_DescriptorSetInstances[spaceID];
			auto& spaceInfo = spaceInfos[spaceID];
			auto descSetAllocator = descriptorPool->GetOrCreate(descSetInst.m_DescriptorSetDesc->GetPoolDesc());
			*descSetInst.p_Set = descSetAllocator->AllocateSet(*descSetInst.p_Layout);
			if (!spaceInfo.m_ResourceStats.m_CBufferBindings.empty())
			{
				descSetInst.m_BoundUniformBuffers.clear();
				for (int cbufID = 0; cbufID < spaceInfo.m_ResourceStats.m_CBufferBindings.size(); ++cbufID)
				{
					descSetInst.m_BoundUniformBuffers.emplace_back();
					auto& binding = descSetInst.m_BoundUniformBuffers.back();
					auto& sourceUniformBuffer = spaceInfo.m_ResourceStats.m_CBufferBindings[cbufID];
					binding.bindingID = sourceUniformBuffer.bindingID;
					binding.bufferStride = sourceUniformBuffer.memoryStride;
					binding.m_UniformBuffers.reserve(sourceUniformBuffer.elementCount);
					for (int elementID = 0; elementID < sourceUniformBuffer.elementCount; ++elementID)
					{
						auto bufferObject = pResourcePool->CreateBufferWithMemory(GPUBufferDescriptor::Create(
							EBufferUsage::eConstantBuffer | EBufferUsage::eDataDst
							, 1, sourceUniformBuffer.memoryStride), vk::MemoryPropertyFlagBits::eDeviceLocal);
						binding.m_UniformBuffers.push_back(bufferObject.buffer);
					}
				}
			}
		}
	}


	//void ShaderBindingInstance::InitShaderBindingSets(FrameBoundResourcePool* pResourcePool)
	//{
	//	m_DescriptorSets.resize(p_ReflectionData->m_BindingData.size());
	//	m_UniformBuffers.clear();
	//	for (int sid = 0; sid < p_ReflectionData->m_BindingData.size(); ++sid)
	//	{
	//		auto& targetDescSet = m_DescriptorSets[sid];
	//		auto& sourceSet = p_ReflectionData->m_BindingData[sid];
	//		uint32_t bindingCount = sourceSet.GetBindingCount();

	//		auto& descSetDesc = m_DescriptorSetDescs[sid];
	//		auto descriptorPool = pResourcePool->descriptorPools.AquirePool();
	//		auto descSetAllocator = descriptorPool->GetOrCreate(descSetDesc->GetPoolDesc());

	//		auto layout = m_DescriptorSetsLayouts[sid];
	//		targetDescSet = descSetAllocator->AllocateSet(layout);

	//		if (!sourceSet.m_UniformBuffers.empty())
	//		{
	//			castl::vector<VKBufferObject> bufferObjects;
	//			bufferObjects.reserve(sourceSet.m_UniformBuffers.size());
	//			for (int ubid = 0; ubid < sourceSet.m_UniformBuffers.size(); ++ubid)
	//			{
	//				auto& sourceUniformBuffer = sourceSet.m_UniformBuffers[ubid];
	//				auto bufferObject = pResourcePool->CreateBufferWithMemory(GPUBufferDescriptor::Create(
	//					EBufferUsage::eConstantBuffer | EBufferUsage::eDataDst
	//					, 1, sourceUniformBuffer.m_Groups[0].m_MemorySize), vk::MemoryPropertyFlagBits::eDeviceLocal);
	//				bufferObjects.push_back(castl::move(bufferObject));
	//			}
	//			m_UniformBuffers.insert(castl::make_pair(sid, bufferObjects));
	//		}
	//	}
	//}

	//void ShaderBindingInstance::InitShaderBindings(CVulkanApplication& application, FrameBoundResourcePool* pResourcePool, ShaderCompilerSlang::ShaderReflectionData const& reflectionData)
	//{
	//	p_Application = &application;
	//	p_ReflectionData = &reflectionData;
	//	m_DescriptorSets.resize(reflectionData.m_BindingData.size());
	//	m_DescriptorSetsLayouts.resize(reflectionData.m_BindingData.size());
	//	for (int sid = 0; sid < reflectionData.m_BindingData.size(); ++sid)
	//	{
	//		auto& targetDescSet = m_DescriptorSets[sid];
	//		auto& sourceSet = reflectionData.m_BindingData[sid];
	//		uint32_t bindingCount = sourceSet.GetBindingCount();

	//		DescriptorSetDesc descSetDesc;
	//		descSetDesc.descs.reserve(bindingCount);
	//		for (auto& uniformBuf : sourceSet.m_UniformBuffers)
	//		{
	//			DescriptorDesc bindingDesc;
	//			bindingDesc.bindingIndex = uniformBuf.m_BindingIndex;
	//			bindingDesc.arraySize = 1;
	//			bindingDesc.descType = vk::DescriptorType::eUniformBuffer;
	//			descSetDesc.descs.push_back(bindingDesc);
	//		}
	//		for (auto& textureBinding : sourceSet.m_Textures)
	//		{
	//			DescriptorDesc bindingDesc;
	//			bindingDesc.bindingIndex = textureBinding.m_BindingIndex;
	//			bindingDesc.arraySize = textureBinding.m_Count;
	//			bindingDesc.descType = vk::DescriptorType::eSampledImage;
	//			descSetDesc.descs.push_back(bindingDesc);
	//		}
	//		for (auto& samplerBinding : sourceSet.m_Samplers)
	//		{
	//			DescriptorDesc bindingDesc;
	//			bindingDesc.bindingIndex = samplerBinding.m_BindingIndex;
	//			bindingDesc.arraySize = samplerBinding.m_Count;
	//			bindingDesc.descType = vk::DescriptorType::eSampler;
	//			descSetDesc.descs.push_back(bindingDesc);
	//		}
	//		for (auto& storageBufferBinding : sourceSet.m_Buffers)
	//		{
	//			DescriptorDesc bindingDesc;
	//			bindingDesc.bindingIndex = storageBufferBinding.m_BindingIndex;
	//			bindingDesc.arraySize = storageBufferBinding.m_Count;
	//			bindingDesc.descType = vk::DescriptorType::eStorageBuffer;
	//			descSetDesc.descs.push_back(bindingDesc);
	//		}
	//		auto descSetLayout = application.GetGPUObjectManager().GetDescriptorSetLayoutCache().GetOrCreate(descSetDesc);
	//		auto descriptorPool = pResourcePool->descriptorPools.AquirePool();
	//		auto descSetAllocator = descriptorPool->GetOrCreate(descSetDesc.GetPoolDesc());

	//		m_DescriptorSetsLayouts[sid] = descSetLayout->GetLayout();
	//		targetDescSet = descSetAllocator->AllocateSet(descSetLayout->GetLayout());

	//		if (!sourceSet.m_UniformBuffers.empty())
	//		{
	//			castl::vector<VKBufferObject> bufferObjects;
	//			bufferObjects.reserve(sourceSet.m_UniformBuffers.size());
	//			for (int ubid = 0; ubid < sourceSet.m_UniformBuffers.size(); ++ubid)
	//			{
	//				auto& sourceUniformBuffer = sourceSet.m_UniformBuffers[ubid];
	//				auto bufferObject = pResourcePool->CreateBufferWithMemory(GPUBufferDescriptor::Create(
	//					EBufferUsage::eConstantBuffer | EBufferUsage::eDataDst
	//					, 1, sourceUniformBuffer.m_Groups[0].m_MemorySize), vk::MemoryPropertyFlagBits::eDeviceLocal);
	//				bufferObjects.push_back(castl::move(bufferObject));
	//			}
	//			m_UniformBuffers.insert(castl::make_pair(sid, bufferObjects));
	//		}
	//	}
	//}

	//void WriteUniformBuffer(ShaderArgList const& shaderArgList
	//	, ShaderCompilerSlang::UniformBufferData const& bufferData
	//	, int32_t bufferGroupID
	//	, char* dataDst)
	//{
	//	auto& group = bufferData.m_Groups[bufferGroupID];
	//	for (auto& element : group.m_Elements)
	//	{
	//		if (auto pDataPos = shaderArgList.FindNumericDataPointer(element.m_Name))
	//		{
	//			//CA_LOG_ERR("Find Binding " + element.m_Name);
	//			uint32_t memoryOffset = element.m_MemoryOffset;
	//			for (uint32_t elementID = 0; elementID < element.m_ElementCount; ++elementID)
	//			{
	//				uint32_t elementOffset = memoryOffset + element.m_Stride * elementID;
	//				memcpy(dataDst + elementOffset, pDataPos, element.m_ElementMemorySize);
	//			}
	//		}
	//	}
	//	for (uint32_t subGroupID : group.m_SubGroups)
	//	{
	//		auto found = shaderArgList.FindSubArgList(bufferData.m_Groups[subGroupID].m_Name);
	//		if (found)
	//		{
	//			WriteUniformBuffer(*found, bufferData, subGroupID, dataDst);
	//		}
	//	}
	//}

	//void WriteResources(CVulkanApplication& application
	//	, ShaderArgList const& shaderArgList
	//	, ShadderResourceProvider& resourceProvider
	//	, ShaderCompilerSlang::ShaderBindingSpaceData const& bindingSpaceData
	//	, int32_t resourceGroupID
	//	, DescritprorWriter& writer
	//	, castl::vector<castl::pair<BufferHandle, ShaderCompilerSlang::EShaderResourceAccess>>& inoutBufferHandles
	//	, castl::vector<castl::pair<ImageHandle, ShaderCompilerSlang::EShaderResourceAccess>>& inoutImageHandles)
	//{
	//	auto& currentGroup = bindingSpaceData.m_ResourceGroups[resourceGroupID];
	//	for (uint32_t texID : currentGroup.m_Textures)
	//	{
	//		auto& textureData = bindingSpaceData.m_Textures[texID];
	//		auto imageHandles = shaderArgList.FindImageHandle(textureData.m_Name);
	//		for (uint32_t imgID = 0; imgID < imageHandles.size(); ++imgID)
	//		{
	//			inoutImageHandles.push_back(castl::make_pair(imageHandles[imgID].first, textureData.m_Access));
	//			auto& imagePair = imageHandles[imgID];
	//			vk::ImageView imageView = resourceProvider.GetImageView(imagePair.first, imagePair.second);

	//			vk::ImageLayout targetLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	//			vk::DescriptorType descriptorType = vk::DescriptorType::eSampledImage;
	//			switch (textureData.m_Access)
	//			{
	//			case ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly:
	//			case ShaderCompilerSlang::EShaderResourceAccess::eReadWrite:
	//				targetLayout = vk::ImageLayout::eGeneral;
	//				descriptorType = vk::DescriptorType::eStorageImage;
	//				break;
	//			}
	//			writer.AddWriteImageView(imageView
	//				, textureData.m_BindingIndex
	//				, descriptorType
	//				//TODO: Readonly Type?
	//				, targetLayout
	//				, imgID);
	//		}
	//	}
	//	for (uint32_t samplerID : currentGroup.m_Samplers)
	//	{
	//		auto& samplerData = bindingSpaceData.m_Samplers[samplerID];
	//		auto samplerDescs = shaderArgList.FindSampler(samplerData.m_Name);
	//		auto sampler = application.GetGPUObjectManager().GetTextureSamplerCache().GetOrCreate(samplerDescs);
	//		writer.AddWriteSampler(sampler->GetSampler(), samplerData.m_BindingIndex);
	//	}
	//	for (uint32_t bufferID : currentGroup.m_Buffers)
	//	{
	//		auto& bufferData = bindingSpaceData.m_Buffers[bufferID];
	//		auto bufferHandles = shaderArgList.FindBufferHandle(bufferData.m_Name);
	//		for (uint32_t bufID = 0; bufID < bufferHandles.size(); ++bufID)
	//		{
	//			inoutBufferHandles.push_back(castl::make_pair(bufferHandles[bufID], bufferData.m_Access));
	//			vk::Buffer buffer = resourceProvider.GetBufferFromHandle(bufferHandles[bufID]);
	//			writer.AddWriteBuffer(buffer, bufferData.m_BindingIndex, vk::DescriptorType::eStorageBuffer, bufID);
	//		}
	//	}

	//	for (uint32_t subGroupID : currentGroup.m_SubGroups)
	//	{
	//		auto found = shaderArgList.FindSubArgList(bindingSpaceData.m_ResourceGroups[subGroupID].m_Name);
	//		if (found)
	//		{
	//			WriteResources(application
	//				, *found
	//				, resourceProvider
	//				, bindingSpaceData
	//				, subGroupID
	//				, writer
	//				, inoutBufferHandles
	//				, inoutImageHandles);
	//		}
	//	}
	//}

	void ShaderBindingInstance::FillShaderData(CVulkanApplication& application
		, ShadderResourceProvider& resourceProvider
		, FrameBoundResourcePool* pResourcePool
		, vk::CommandBuffer& command
		, castl::vector<castl::unordered_map<cacore::NameHash, castl::shared_ptr<ShaderStruct>> const*> const& shaderStructs)
	{
		auto& spaceInfos = p_ReflectionData->m_BindingInfo.m_SpaceInfos;
		castl::vector<DescritprorWriter> writers;
		writers.resize(spaceInfos.size());
		for (int spaceID = 0; spaceID < spaceInfos.size(); ++spaceID)
		{
			auto& spaceInfo = spaceInfos[spaceID];
			auto& spaceStats = spaceInfo.m_ResourceStats;
			auto& descSetInst = m_DescriptorSetInstances[spaceID];
			auto& writer = writers[spaceID];
			writer.Initialize(*descSetInst.p_Set
				, spaceStats.m_RWTextureCount + spaceStats.m_TextureCount
				, spaceStats.m_SamplerCount
				, spaceStats.m_CBufferCount
				, spaceStats.m_StorageBufferCount);
		}
		//目前仅支持搜索根节点下的ConstantBuffer和ParameterBlock
		auto& bindingInfo = p_ReflectionData->m_BindingInfo;

		auto& rootHierarchy = bindingInfo.m_BindingDataHierarchies[bindingInfo.m_RootHierarchyID];

		auto findShaderStructOfName = [&](cacore::NameHash const& inName)
		{
			for (auto pMap : shaderStructs)
			{
				auto& structMap = *pMap;
				auto found = structMap.find(inName);
				if (found != structMap.end())
				{
					return static_cast<VKShaderStruct const*>(found->second.get());
				}
			}
			return static_cast<VKShaderStruct const*>(nullptr);
		};

		auto selectImageBindingLayoutAndDescriptorType = [&](ShaderCompilerSlang::EShaderResourceAccess accessType
			, vk::ImageLayout& outTargetLayout, vk::DescriptorType& outDescriptorType)
		{
			switch (accessType)
			{
			case ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly:
			case ShaderCompilerSlang::EShaderResourceAccess::eReadWrite:
				outTargetLayout = vk::ImageLayout::eGeneral;
				outDescriptorType = vk::DescriptorType::eStorageImage;
				break;
			default:
				outTargetLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
				outDescriptorType = vk::DescriptorType::eSampledImage;
				break;
			}
		};

		castl::deque<uint32_t> hierarchyIDs;
		hierarchyIDs.insert(hierarchyIDs.end(), rootHierarchy.m_SubBindingHierarchies.begin(), rootHierarchy.m_SubBindingHierarchies.end());
		while (!hierarchyIDs.empty())
		{
			uint32_t hierarchyID = hierarchyIDs.front();
			hierarchyIDs.pop_front();
			auto& hierarchy = bindingInfo.m_BindingDataHierarchies[hierarchyID];
			hierarchyIDs.insert(hierarchyIDs.end(), hierarchy.m_SubBindingHierarchies.begin(), hierarchy.m_SubBindingHierarchies.end());
			auto sourceStruct = findShaderStructOfName(hierarchy.m_Name);
			CA_ASSERT_BREAK(sourceStruct != nullptr, "cant find shader struct: {}", hierarchy.m_Name);

			//Write Uniform Buffer
			if (hierarchy.m_SelfUniformBufferID != -1)
			{
				CA_ASSERT_BREAK(hierarchy.m_SelfUniformSpaceID != -1, "invalid uniform space id: {}", hierarchy.m_SelfUniformSpaceID);
				auto& descSetInst = m_DescriptorSetInstances[hierarchy.m_SelfUniformSpaceID];
				auto& writer = writers[hierarchy.m_SelfUniformSpaceID];
				auto& uniformBufferData = sourceStruct->GetSelfUniformBuffer();
				ShaderDescriptorSetInstance::ShaderUniformBufferBindings* pdestUniforms = descSetInst.GetUniformBufferBinding(hierarchy.m_SelfUniformBufferID);
				CA_ASSERT_BREAK(pdestUniforms != nullptr, "cant find self uniform buffer binding: {}", hierarchy.m_Name);
				pdestUniforms->bufferStride;
				for (int uniformID = 0; uniformID < pdestUniforms->m_UniformBuffers.size(); ++uniformID)
				{
					auto& bufferHandle = pdestUniforms->m_UniformBuffers[uniformID];
					uint32_t offset = pdestUniforms->bufferStride * uniformID;
					auto stageBuffer = pResourcePool->CreateStagingBuffer(pdestUniforms->bufferStride, EBufferUsage::eDataSrc);
					auto tmpMap = pResourcePool->memoryManager.ScopedMapMemory(stageBuffer.allocation);
					memcpy(tmpMap.mappedMemory, &uniformBufferData[offset], pdestUniforms->bufferStride);
					command.copyBuffer(stageBuffer.buffer, bufferHandle, vk::BufferCopy(0, 0, pdestUniforms->bufferStride));
				}
				writer.AddWriteBuffers(pdestUniforms->m_UniformBuffers, hierarchy.m_SelfUniformBufferID, vk::DescriptorType::eUniformBuffer, 0, pdestUniforms->m_UniformBuffers.size());
			}
		
			//Write Resources
			if (!hierarchy.m_Bindings.empty())
			{
				auto& bufferHandles = sourceStruct->GetBufferHandles();
				auto& imageHandles = sourceStruct->GetImageHandles();
				auto& samplerDescs = sourceStruct->GetSamplerDescriptors();

				for (auto& binding : hierarchy.m_Bindings)
				{
					switch (binding.m_ResourceType)
					{
					case ShaderCompilerSlang::EShaderResourceType::eTexture:
					case ShaderCompilerSlang::EShaderResourceType::eRWTexture:
					{
						auto found = imageHandles.find(binding.m_Name);
						if (found != imageHandles.end())
						{
							auto& writer = writers[binding.m_BindingSpace];

							vk::ImageLayout targetLayout;
							vk::DescriptorType descriptorType;
							selectImageBindingLayoutAndDescriptorType(binding.m_Access, targetLayout, descriptorType);

							auto& imageList = found->second;
							for (uint32_t imageID = 0; imageID < imageList.size(); ++imageID)
							{
								auto image = resourceProvider.GetImageView(imageList[imageID].first, imageList[imageID].second);
								writer.AddWriteImageView(image, binding.m_BindingID, descriptorType, targetLayout, imageID);
								m_ImageHandles.push_back(castl::make_pair(imageList[imageID].first, binding.m_Access));
							}
						}
						break;
					}
					case ShaderCompilerSlang::EShaderResourceType::eStructuredBuffer:
					case ShaderCompilerSlang::EShaderResourceType::eRWStructuredBuffer:
					{
						auto found = bufferHandles.find(binding.m_Name);
						if (found != bufferHandles.end())
						{
							auto& writer = writers[binding.m_BindingSpace];

							vk::ImageLayout targetLayout;
							vk::DescriptorType descriptorType;
							selectImageBindingLayoutAndDescriptorType(binding.m_Access, targetLayout, descriptorType);

							auto& bufferList = found->second;
							for (uint32_t bufferID = 0; bufferID < bufferList.size(); ++bufferID)
							{
								auto buffer = resourceProvider.GetBufferFromHandle(bufferList[bufferID]);
								writer.AddWriteBuffer(buffer, binding.m_BindingID, vk::DescriptorType::eStorageBuffer, bufferID);
								m_BufferHandles.push_back(castl::make_pair(bufferList[bufferID], binding.m_Access));
							}
						}
						break;
					}
					case ShaderCompilerSlang::EShaderResourceType::eSampler:
					{
						auto found = samplerDescs.find(binding.m_Name);
						if (found != samplerDescs.end())
						{
							auto& writer = writers[binding.m_BindingSpace];

							auto& samplerList = found->second;
							for (uint32_t samplerID = 0; samplerID < samplerList.size(); ++samplerID)
							{
								auto sampler = application.GetGPUObjectManager().GetTextureSamplerCache().GetOrCreate(samplerList[samplerID]);
								writer.AddWriteSampler(sampler->GetSampler(), binding.m_BindingID, samplerID);
							}
						}
					}
					}
				}
			}
		}

		//Apply Writes
		for (int spaceID = 0; spaceID < spaceInfos.size(); ++spaceID)
		{
			auto& writer = writers[spaceID];
			writer.Apply(application.GetDevice());
		}

		//PushUniformReadyBarriers(m_PrepareShaderBindingsBarrierCollector, ResourceUsage::eVertexRead | ResourceUsage::eFragmentRead | ResourceUsage::eComputeRead);
	}

	void ShaderBindingInstance::PushUniformReadyBarriers(VulkanBarrierCollector& targetBarrierCollector, ResourceUsageFlags destUsage)
	{
		for (auto& descSetInst : m_DescriptorSetInstances)
		{
			for (auto& uniformBufferInst : descSetInst.m_BoundUniformBuffers)
			{
				for (auto& uniformBuffer : uniformBufferInst.m_UniformBuffers)
				{
					targetBarrierCollector.PushBufferBarrier(uniformBuffer, ResourceUsage::eTransferDest, destUsage);
				}
			}
		}
	}

	//void ShaderBindingInstance::FillShaderData(CVulkanApplication& application
	//	, ShadderResourceProvider& resourceProvider
	//	, FrameBoundResourcePool* pResourcePool
	//	, vk::CommandBuffer& command
	//	, castl::vector <castl::pair <castl::string, castl::shared_ptr<ShaderArgList>>> const& shaderArgLists)
	//{
	//	if (p_ReflectionData == nullptr)
	//		return;
	//	for (int sid = 0; sid < p_ReflectionData->m_BindingData.size(); ++sid)
	//	{
	//		DescritprorWriter writer;

	//		auto& targetDescSet = m_DescriptorSets[sid];
	//		auto& sourceSet = p_ReflectionData->m_BindingData[sid];

	//		writer.Initialize(targetDescSet, sourceSet.m_Textures.size(), sourceSet.m_Samplers.size(), sourceSet.m_UniformBuffers.size(), sourceSet.m_Buffers.size());

	//		//Uniform Buffers
	//		auto foundUniformBuffers = m_UniformBuffers.find(sid);
	//		if (foundUniformBuffers != m_UniformBuffers.end())
	//		{
	//			auto& uniformBuffers = foundUniformBuffers->second;
	//			for (int ubid = 0; ubid < sourceSet.m_UniformBuffers.size(); ++ubid)
	//			{
	//				auto& sourceUniformBuffer = sourceSet.m_UniformBuffers[ubid];
	//				auto& bufferHandle = uniformBuffers[ubid];
	//				auto& group = sourceUniformBuffer.m_Groups[0];
	//				writer.AddWriteBuffer(bufferHandle.buffer, sourceUniformBuffer.m_BindingIndex, vk::DescriptorType::eUniformBuffer, 0);
	//				vk::DeviceSize memorySize = group.m_MemorySize;
	//				auto stageBuffer = pResourcePool->CreateStagingBuffer(memorySize, EBufferUsage::eDataSrc);

	//				{
	//					auto tmpMap = pResourcePool->memoryManager.ScopedMapMemory(stageBuffer.allocation);
	//					for (auto shaderArgList : shaderArgLists)
	//					{
	//						if (shaderArgList.first == group.m_Name || (group.m_Name == "__Global" && shaderArgList.first.empty()))
	//						{
	//							WriteUniformBuffer(*shaderArgList.second, sourceUniformBuffer, 0, static_cast<char*>(tmpMap.mappedMemory));
	//						}
	//						//Global下的subgroup可以被认为是单独的资源组
	//						else if (group.m_Name == "__Global")
	//						{
	//							for (uint32_t subgroupID : group.m_SubGroups)
	//							{
	//								auto& subgroup = sourceUniformBuffer.m_Groups[subgroupID];
	//								if (subgroup.m_Name == shaderArgList.first)
	//								{
	//									WriteUniformBuffer(*shaderArgList.second, sourceUniformBuffer, subgroupID, static_cast<char*>(tmpMap.mappedMemory));
	//								}
	//							}
	//						}
	//					}
	//				}
	//				command.copyBuffer(stageBuffer.buffer, bufferHandle.buffer, vk::BufferCopy(0, 0, memorySize));
	//			}
	//		}

	//		//Non Uniform Buffer Resources
	//		if (!sourceSet.m_ResourceGroups.empty())
	//		{
	//			for (auto shaderArgList : shaderArgLists)
	//			{
	//				auto& group = sourceSet.m_ResourceGroups[0];
	//				if (shaderArgList.first == group.m_Name || (group.m_Name == "__Global" && shaderArgList.first.empty()))
	//				{
	//					WriteResources(
	//						application
	//						, *shaderArgList.second
	//						, resourceProvider
	//						, sourceSet
	//						, 0
	//						, writer
	//						, m_BufferHandles
	//						, m_ImageHandles);
	//				}
	//				//Global下的subgroup可以被认为是单独的资源组
	//				else if (group.m_Name == "__Global")
	//				{
	//					for (uint32_t subgroupID : group.m_SubGroups)
	//					{
	//						auto& subgroup = sourceSet.m_ResourceGroups[subgroupID];
	//						if (subgroup.m_Name == shaderArgList.first)
	//						{
	//							WriteResources(
	//								application
	//								, *shaderArgList.second
	//								, resourceProvider
	//								, sourceSet
	//								, subgroupID
	//								, writer
	//								, m_BufferHandles
	//								, m_ImageHandles);
	//						}
	//					}
	//				}
	//			}
	//		}
	//		writer.Apply(application.GetDevice());
	//	}
	//}

}

