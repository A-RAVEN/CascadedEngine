#include <RenderBackend_D3D12.h>
#include <Utils/InterfaceTranslation.h>
#include "GPUResourceBindingInstance.h"

namespace graphics_backend
{

	BufferHandle GPUConstantBufferManager::GetConstantBufferHandle(D3D2ShaderStruct const* pShaderStruct)
	{
		return m_ConstantBufferHandles.get_or_create(pShaderStruct, [&](D3D2ShaderStruct const* inKey) -> BufferHandle
		{
			return BufferHandle(inKey->GetStructTypeName(), true);
		})->second;
	}

	void GPUConstantBufferManager::BuildResources(D3D12GraphLocalResourceManager& resourceManager)
	{
		m_ConstantBufferHandles.for_each_const([&](D3D2ShaderStruct const* inKey, BufferHandle const& inValue)
		{
			ShaderCompilerSlang::ShaderStructData const* pStructData = inKey->GetStructData();
			GPUBufferDescriptor desc = GPUBufferDescriptor::Create(EBufferUsage::eConstantBuffer | EBufferUsage::eDataDst, 1, pStructData->m_StructUniforms.m_Stride);
			resourceManager.AddBuffer(inValue, desc);
		});
	}

	void GPUConstantBufferManager::IterateResources(castl::function<void(D3D2ShaderStruct const*, BufferHandle const&)> callback)
	{
		m_ConstantBufferHandles.for_each_const([&](D3D2ShaderStruct const* inKey, BufferHandle const& inValue)
		{
			callback(inKey, inValue);
		});
	}

	void ShaderResourceSet::Init(RenderBackend_D3D12* app
		, ShaderInfo const& shaderInfo
		, castl::array_ref<ShaderStructDic const*> const& shaderStructs)
	{

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

		auto& reflectionData = app->GetShaderFileInfo(shaderInfo)->reflectionData;
		this->shaderInfo = shaderInfo;
		resourceDic.clear();

		auto& bindingInfo = reflectionData.m_BindingInfo;
		auto& rootHierarchy = bindingInfo.m_BindingDataHierarchies[bindingInfo.m_RootHierarchyID];
		castl::deque<uint32_t> hierarchyIDs;
		hierarchyIDs.insert(hierarchyIDs.end(), rootHierarchy.m_SubBindingHierarchies.begin(), rootHierarchy.m_SubBindingHierarchies.end());
		while (!hierarchyIDs.empty())
		{
			uint32_t hierarchyID = hierarchyIDs.front();
			hierarchyIDs.pop_front();
			auto& hierarchy = bindingInfo.m_BindingDataHierarchies[hierarchyID];
			hierarchyIDs.insert(hierarchyIDs.end(), hierarchy.m_SubBindingHierarchies.begin(), hierarchy.m_SubBindingHierarchies.end());
			auto sourceStruct = findShaderStructOfName(hierarchy.m_Name);
			resourceDic.insert(castl::make_pair(hierarchy.m_Name, sourceStruct));
		}
	}

	void GPUResourceBindingInstance::Init(ShaderResourceSet const& resourceSet
		, GPUConstantBufferManager& cbufferManager)
	{
		shaderInfo = resourceSet.shaderInfo;
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


		auto& bindingInfo = p_ReflectionData->m_BindingInfo;

		auto& rootHierarchy = bindingInfo.m_BindingDataHierarchies[bindingInfo.m_RootHierarchyID];

		auto findShaderStructOfName = [&](cacore::NameHash const& inName)
		{
			auto found = resourceSet.resourceDic.find(inName);
			if (found != resourceSet.resourceDic.end())
			{
				return found->second;
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
				spaceResourceInfo.cbufferHandles[uniformBufferID] = cbufferManager.GetConstantBufferHandle(sourceStruct);
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
							ImageBindingInfo imageInfo{};
							imageInfo.accessType = binding.m_Access;
							imageInfo.resourceType = binding.m_ResourceType;

							auto& imageList = found->second;
							for (uint32_t imageID = 0; imageID < imageList.size(); ++imageID)
							{
								imageInfo.image = imageList[imageID].first;
								imageInfo.textureView = imageList[imageID].second;
								spaceResourceInfo.imageInfo.push_back(imageInfo);
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
							BufferBindingInfo bufferInfo{};
							bufferInfo.accessType = binding.m_Access;
							bufferInfo.resourceType = binding.m_ResourceType;

							auto& bufferList = found->second;
							for (uint32_t bufferID = 0; bufferID < bufferList.size(); ++bufferID)
							{
								bufferInfo.buffer = bufferList[bufferID];
								spaceResourceInfo.bufferInfos.push_back(bufferInfo);
							}
						}
						break;
					}
					case ShaderCompilerSlang::EShaderResourceType::eSampler:
					{
						auto found = samplerDescs.find(binding.m_Name);
						if (found != samplerDescs.end())
						{
							auto& samplerList = found->second;
						}
					}
					}
				}
			}
		}
	}
	void GPUResourceBindingInstance::BuildResources(GPUGraph const& gpuGraph, D3D12GraphLocalResourceManager& resourceManager)
	{
		for (auto& spaceInfo : m_GPUResourceSpaceInfos)
		{
			auto& spaceRefInfo = p_ReflectionData->m_BindingInfo.m_SpaceInfos[spaceInfo.spaceID];
			for (auto& bufInfo : spaceInfo.bufferInfos)
			{
				auto* pDesc = gpuGraph.GetBufferManager().GetDescriptor(bufInfo.buffer.GetKey());
				CA_ASSERT_BREAK(pDesc != nullptr, "Buffer {} Not Registered", bufInfo.buffer.GetName());
				resourceManager.AddBuffer(bufInfo.buffer, *pDesc);
			}
			for (auto& imgInfo : spaceInfo.imageInfo)
			{
				auto* pDesc = gpuGraph.GetImageManager().GetDescriptor(imgInfo.image.GetKey());
				CA_ASSERT_BREAK(pDesc != nullptr, "Image {} Not Registered", imgInfo.image.GetName());
				resourceManager.AddTexture(imgInfo.image, *pDesc, imgInfo.textureView);
			}
			// if (!spaceInfo.cbufferStructs.empty())
			// {
			// 	spaceInfo.cbufferHandles.resize(spaceInfo.cbufferStructs.size());
			// 	CA_ASSERT_BREAK(spaceRefInfo.m_ResourceStats.m_CBufferBindings.size() == spaceInfo.cbufferStructs.size(), "CBuffer Size Not Equal");
			// 	for (uint32_t bufID = 0; bufID < spaceInfo.cbufferStructs.size(); ++bufID)
			// 	{
			// 		auto& cBuffer = spaceInfo.cbufferHandles[bufID];
			// 		auto cBufStruct = spaceInfo.cbufferStructs[bufID];
			// 		auto bindingDesc = spaceRefInfo.m_ResourceStats.m_CBufferBindings[bufID];
			// 		cBuffer = BufferHandle{ CANAME("__internal_cbuffer_"), resourceManager.InternalResourceID() };
			// 		GPUBufferDescriptor desc = GPUBufferDescriptor::Create(EBufferUsage::eConstantBuffer | EBufferUsage::eDataDst, bindingDesc.elementCount, bindingDesc.memoryStride);
			// 		resourceManager.AddBuffer(cBuffer, desc);
			// 	}
			// }
		}
	}

	void GPUResourceBindingInstance::IterateResourceUsages(castl::function<void(ImageBindingInfo const&)>const& imageCallback,
	castl::function<void(BufferBindingInfo const&)>const& bufferCallback) const
	{
		for (auto& spaceInfo : m_GPUResourceSpaceInfos)
		{
			auto& spaceRefInfo = p_ReflectionData->m_BindingInfo.m_SpaceInfos[spaceInfo.spaceID];
			for (auto& bufInfo : spaceInfo.bufferInfos)
			{
				bufferCallback(bufInfo);
			}
			for (auto& imgInfo : spaceInfo.imageInfo)
			{
				imageCallback(imgInfo);
			}
			if (!spaceInfo.cbufferStructs.empty())
			{
				CA_ASSERT_BREAK(spaceRefInfo.m_ResourceStats.m_CBufferBindings.size() == spaceInfo.cbufferStructs.size(), "CBuffer Size Not Equal");
				for (uint32_t bufID = 0; bufID < spaceInfo.cbufferStructs.size(); ++bufID)
				{
					auto& cBuffer = spaceInfo.cbufferHandles[bufID];

					BufferBindingInfo bufferInfo{};
					bufferInfo.accessType = ShaderCompilerSlang::EShaderResourceAccess::eReadOnly;
					bufferInfo.resourceType = ShaderCompilerSlang::EShaderResourceType::eCBuffer;
					bufferInfo.buffer = cBuffer;
					bufferCallback(bufferInfo);
				}
			}
		}
	}

}