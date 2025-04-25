#include "GPUResourceBindingInstance.h"
#include <RenderBackend_D3D12.h>
#include <Utils/InterfaceTranslation.h>

namespace graphics_backend
{
	void GPUResourceBindingInstance::Init(ShaderInfo const& shaderInfo
		, castl::array_ref<ShaderStructDic const*> const& shaderStructs)
	{
		p_ShaderInfo = &shaderInfo;
		p_ReflectionData = &GetApp()->GetShaderFileInfo(shaderInfo)->reflectionData;

		auto& spaceInfos = p_ReflectionData->m_BindingInfo.m_SpaceInfos;
		m_GPUResourceSpaceInfos.resize(spaceInfos.size());
		for (int spaceID = 0; spaceID < spaceInfos.size(); ++spaceID)
		{
			m_GPUResourceSpaceInfos[spaceID].spaceID = spaceID;
			auto& spaceInfo = spaceInfos[spaceID];
			auto& spaceStats = spaceInfo.m_ResourceStats;
			m_GPUResourceSpaceInfos[spaceID].cbufferStructs.resize(spaceStats.m_CBufferBindings.size());
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
					return static_cast<D3D2ShaderStruct const*>(found->second.get());
				}
			}
			return static_cast<D3D2ShaderStruct const*>(nullptr);
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
				auto spaceID = hierarchy.m_SelfUniformSpaceID;
				auto uniformBufferID = hierarchy.m_SelfUniformSpaceID;
				auto& spaceResourceInfo = m_GPUResourceSpaceInfos[spaceID];
				spaceResourceInfo.cbufferStructs[uniformBufferID] = sourceStruct;
			}

			//Write Resources
			if (!hierarchy.m_Bindings.empty())
			{
				auto& bufferHandles = sourceStruct->GetBufferHandles();
				auto& imageHandles = sourceStruct->GetImageHandles();
				auto& samplerDescs = sourceStruct->GetSamplerDescriptors();

				for (auto& binding : hierarchy.m_Bindings)
				{
					auto spaceID = binding.m_BindingSpace;
					auto& spaceResourceInfo = m_GPUResourceSpaceInfos[spaceID];
					switch (binding.m_ResourceType)
					{
					case ShaderCompilerSlang::EShaderResourceType::eTexture:
					case ShaderCompilerSlang::EShaderResourceType::eRWTexture:
					{
						auto found = imageHandles.find(binding.m_Name);
						if (found != imageHandles.end())
						{
							auto& imageList = found->second;
							for (uint32_t imageID = 0; imageID < imageList.size(); ++imageID)
							{
								spaceResourceInfo.imageInfo.insert(castl::make_pair(imageList[imageID].first, binding.m_ResourceType));
								//auto image = resourceProvider.GetImageView(imageList[imageID].first, imageList[imageID].second);
								//writer.AddWriteImageView(image, binding.m_BindingID, descriptorType, targetLayout, imageID);
								//m_ImageHandles.push_back(castl::make_pair(imageList[imageID].first, binding.m_Access));
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

							auto& bufferList = found->second;
							for (uint32_t bufferID = 0; bufferID < bufferList.size(); ++bufferID)
							{
								spaceResourceInfo.bufferInfos.insert(castl::make_pair(bufferList[bufferID], binding.m_ResourceType));
							}
						}
						break;
					}
					case ShaderCompilerSlang::EShaderResourceType::eSampler:
					{
						auto found = samplerDescs.find(binding.m_Name);
						if (found != samplerDescs.end())
						{
							//auto& writer = writers[binding.m_BindingSpace];

							auto& samplerList = found->second;
							//for (uint32_t samplerID = 0; samplerID < samplerList.size(); ++samplerID)
							//{
							//	auto sampler = application.GetGPUObjectManager().GetTextureSamplerCache().GetOrCreate(samplerList[samplerID]);
							//	writer.AddWriteSampler(sampler->GetSampler(), binding.m_BindingID, samplerID);
							//}
						}
					}
					}
				}
			}
		}
	}
	void GPUResourceBindingInstance::BuildResources(GPUGraph& gpuGraph, D3D12GraphLocalResourceManager& resourceManager)
	{
		for (auto& spaceInfo : m_GPUResourceSpaceInfos)
		{
			auto& spaceRefInfo = p_ReflectionData->m_BindingInfo.m_SpaceInfos[spaceInfo.spaceID];
			for (auto& bufInfo : spaceInfo.bufferInfos)
			{
				auto& buffer = bufInfo.first;
				auto* pDesc = gpuGraph.GetBufferManager().GetDescriptor(buffer.GetKey());
				CA_ASSERT_BREAK(pDesc != nullptr, "Buffer {} Not Registered", buffer.GetName());
				resourceManager.AddBuffer(buffer, *pDesc);
			}
			for (auto& imgInfo : spaceInfo.imageInfo)
			{
				auto& img = imgInfo.first;
				auto* pDesc = gpuGraph.GetImageManager().GetDescriptor(img.GetKey());
				CA_ASSERT_BREAK(pDesc != nullptr, "Image {} Not Registered", img.GetName());
				resourceManager.AddTexture(img, *pDesc);
			}
			if (!spaceInfo.cbufferStructs.empty())
			{
				spaceInfo.cbufferHandles.resize(spaceInfo.cbufferStructs.size());
				CA_ASSERT_BREAK(spaceRefInfo.m_ResourceStats.m_CBufferBindings.size() == spaceInfo.cbufferStructs.size(), "CBuffer Size Not Equal");
				for (uint32_t bufID = 0; bufID < spaceInfo.cbufferStructs.size(); ++bufID)
				{
					auto& cBuffer = spaceInfo.cbufferHandles[bufID];
					auto cBufStruct = spaceInfo.cbufferStructs[bufID];
					auto bindingDesc = spaceRefInfo.m_ResourceStats.m_CBufferBindings[bufID];
					cBuffer = BufferHandle{ CANAME("__internal_cbuffer_"), resourceManager.InternalResourceID() };
					GPUBufferDescriptor desc = GPUBufferDescriptor::Create(EBufferUsage::eConstantBuffer | EBufferUsage::eDataDst, bindingDesc.elementCount, bindingDesc.memoryStride);
					resourceManager.AddBuffer(cBuffer, desc);
				}
			}
		}
	}
}