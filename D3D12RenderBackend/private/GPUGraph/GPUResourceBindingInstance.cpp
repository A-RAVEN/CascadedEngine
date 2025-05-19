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
			m_GPUResourceSpaceInfos[spaceID].cbufferInfos.resize(spaceStats.m_CBufferBindings.size());
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

			//Collect Uniform Buffer
			if (hierarchy.m_SelfUniformBufferID != -1)
			{
				CA_ASSERT_BREAK(hierarchy.m_SelfUniformSpaceID != -1, "invalid uniform space id: {}", hierarchy.m_SelfUniformSpaceID);
				auto spaceID = hierarchy.m_SelfUniformSpaceID;
				auto uniformBufferID = hierarchy.m_SelfUniformSpaceID;
				auto& spaceResourceInfo = m_GPUResourceSpaceInfos[spaceID];
				auto& cbufferInfo = spaceResourceInfo.cbufferInfos[uniformBufferID];
				cbufferInfo.bindingID = hierarchy.m_SelfUniformBufferID;
				cbufferInfo.pCBufferStruct = sourceStruct;
				cbufferInfo.cbufferHandle = cbufferManager.GetConstantBufferHandle(sourceStruct);
			}

			//Collect Resources
			if (!hierarchy.m_Bindings.empty())
			{
				auto& bufferHandles = sourceStruct->GetBufferHandles();
				auto& imageHandles = sourceStruct->GetImageHandles();
				auto& samplerDescs = sourceStruct->GetSamplerDescriptors();

				for (auto& binding : hierarchy.m_Bindings)
				{
					auto spaceID = binding.m_BindingSpace;
					auto bindingID = binding.m_BindingID;
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
							imageInfo.bindingID = bindingID;
							auto& imageList = found->second;
							for (uint32_t imageID = 0; imageID < imageList.size(); ++imageID)
							{
								ImageBindingInfo::ImageBinding binding;
								binding.image = imageList[imageID].first;
								binding.textureView = imageList[imageID].second;
								imageInfo.bindings.push_back(binding);
							}
							spaceResourceInfo.imageInfo.push_back(imageInfo);
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
							bufferInfo.bindingID = bindingID;

							auto& bufferList = found->second;
							for (uint32_t bufferID = 0; bufferID < bufferList.size(); ++bufferID)
							{
								bufferInfo.bindings.push_back(bufferList[bufferID]);
							}
							spaceResourceInfo.bufferInfos.push_back(bufferInfo);
						}
						break;
					}
					case ShaderCompilerSlang::EShaderResourceType::eSampler:
					{
						auto found = samplerDescs.find(binding.m_Name);
						if (found != samplerDescs.end())
						{
							auto& samplerList = found->second;
							SamplerBindingInfo samplerInfo{};
							samplerInfo.bindingID = bindingID;
							samplerInfo.samplerCount = samplerList.size();
							spaceResourceInfo.samplerInfos.push_back(samplerInfo);
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
				for (auto& buf : bufInfo.bindings)
				{
					auto* pDesc = gpuGraph.GetBufferManager().GetDescriptor(buf.GetKey());
					CA_ASSERT_BREAK(pDesc != nullptr, "Buffer {} Not Registered", buf.GetName());
					resourceManager.AddBuffer(buf, *pDesc);
				}
			}
			for (auto& imgInfo : spaceInfo.imageInfo)
			{
				for (auto& img : imgInfo.bindings)
				{
					auto* pDesc = gpuGraph.GetImageManager().GetDescriptor(img.image.GetKey());
					CA_ASSERT_BREAK(pDesc != nullptr, "Image {} Not Registered", img.image.GetName());
					resourceManager.AddTexture(img.image, *pDesc, img.textureView);
				}
			}
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
			if (!spaceInfo.cbufferInfos.empty())
			{
				CA_ASSERT_BREAK(spaceRefInfo.m_ResourceStats.m_CBufferBindings.size() == spaceInfo.cbufferInfos.size(), "CBuffer Size Not Equal");
				for (uint32_t bufID = 0; bufID < spaceInfo.cbufferInfos.size(); ++bufID)
				{
					auto& cBuffer = spaceInfo.cbufferInfos[bufID];

					BufferBindingInfo bufferInfo{};
					bufferInfo.accessType = ShaderCompilerSlang::EShaderResourceAccess::eReadOnly;
					bufferInfo.resourceType = ShaderCompilerSlang::EShaderResourceType::eCBuffer;
					bufferInfo.bindingID = cBuffer.bindingID;
					bufferInfo.bindings = { cBuffer.cbufferHandle };

					bufferCallback(bufferInfo);
				}
			}
		}
	}

	void GPUResourceBindingInstance::BindDescriptors(D3D12GraphLocalResourceManager& resourceManager
		, GPUDescriptorHeap& gpuDescriptorHeap)
	{
		for (size_t spaceID = 0; spaceID < m_GPUResourceSpaceInfos.size(); ++spaceID)
		{
			auto& spaceInfo = m_GPUResourceSpaceInfos[spaceID];
			std::vector<D3D12_DESCRIPTOR_RANGE1> descriptors;
			uint32_t descriptorCount = 0;
			for (auto& cbuffer : spaceInfo.cbufferInfos)
			{
				CD3DX12_DESCRIPTOR_RANGE1 range;
				range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, cbuffer.bindingID);
				descriptors.push_back(range);
				descriptorCount++;
			}
			for (auto& bufferInfo : spaceInfo.bufferInfos)
			{
				CD3DX12_DESCRIPTOR_RANGE1 range;
				range.Init(bufferInfo.isUAV() ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV
					, bufferInfo.bindings.size()
					, bufferInfo.bindingID);
				descriptors.push_back(range);
				descriptorCount += bufferInfo.bindings.size();
			}
			for (auto& imgInfo : spaceInfo.imageInfo)
			{
				CD3DX12_DESCRIPTOR_RANGE1 range;
				range.Init(imgInfo.isUAV() ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV
					, imgInfo.bindings.size()
					, imgInfo.bindingID);
				descriptors.push_back(range);
				descriptorCount += imgInfo.bindings.size();
			}
			spaceInfo.descriptorAllocation = gpuDescriptorHeap.AllocDescriptorChunk(descriptorCount);
			{
				uint32_t descriptorID = 0;
				for (auto& cbuffer : spaceInfo.cbufferInfos)
				{
					//Desc Table Range
					CD3DX12_DESCRIPTOR_RANGE1 range;
					range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, cbuffer.bindingID);
					descriptors.push_back(range);
					//Copy CPU Desc To GPU DescHeap
					auto pResource = resourceManager.GetBufferResource(cbuffer.cbufferHandle);
					CA_ASSERT_BREAK(pResource != nullptr, "CBuffer {} Resource Not Found", cbuffer.cbufferHandle.GetName());
					GetDevice()->CopyDescriptorsSimple(1, spaceInfo.descriptorAllocation.Slice(descriptorID).CPUHandle()
						, pResource->cbv.CPUHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					descriptorID++;
				}
				for (auto& bufferInfo : spaceInfo.bufferInfos)
				{
					//Desc Table Range
					CD3DX12_DESCRIPTOR_RANGE1 range;
					range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, bufferInfo.bindings.size(), bufferInfo.bindingID);
					descriptors.push_back(range);

					//Copy CPU Descs To GPU DescHeap
					bool isUAV = bufferInfo.isUAV();
					for (auto& buf : bufferInfo.bindings)
					{
						auto pResource = resourceManager.GetBufferResource(buf);
						CA_ASSERT_BREAK(pResource != nullptr, "ShaderBuffer {} Resource Not Found", buf.GetName());
						auto writingView = isUAV ? pResource->uav : pResource->srv;
						GetDevice()->CopyDescriptorsSimple(1, spaceInfo.descriptorAllocation.Slice(descriptorID).CPUHandle()
							, writingView.CPUHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
						descriptorID++;
					}
				}
				for (auto& imageInfo : spaceInfo.imageInfo)
				{
					//Desc Table Range
					CD3DX12_DESCRIPTOR_RANGE1 range;
					range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, imageInfo.bindings.size(), imageInfo.bindingID);
					descriptors.push_back(range);

					//Copy CPU Descs To GPU DescHeap
					bool isUAV = imageInfo.isUAV();
					for (auto& img : imageInfo.bindings)
					{
						auto pResource = resourceManager.GetImageResource(img.image);
						CA_ASSERT_BREAK(pResource != nullptr, "ShaderImage {} Resource Not Found", img.image.GetName());
						auto foundView = pResource->resourceViews.find(img.textureView);
						CA_ASSERT_BREAK(foundView != pResource->resourceViews.end(), "ShaderImageView {} Resource Not Found", img.image.GetName());

						auto writingView = isUAV ? foundView->second.uav : foundView->second.srv;
						GetDevice()->CopyDescriptorsSimple(1, spaceInfo.descriptorAllocation.Slice(descriptorID).CPUHandle()
							, writingView.CPUHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
						descriptorID++;
					}
				}
			}
			
		}
	}

}