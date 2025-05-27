#include <RenderBackend_D3D12.h>
#include <Utils/InterfaceTranslation.h>
#include "GPUResourceBindingInstance.h"
#include <D3D12Debug.h>
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


	struct HierarchyBound
	{
		ShaderCompilerSlang::ShaderBindingHierarchy const* pHierarchy;
		D3D2ShaderStruct const* pStruct;
		uint32_t offset;
	};

	void ReflectResourceBindings(ShaderCompilerSlang::ShaderReflectionData const* reflectionData
		, castl::unordered_map<cacore::NameHash, D3D2ShaderStruct const*> const& resoureBindings
		, castl::function<void(HierarchyBound const&)> hierarchyBoundCallback)
	{
		using namespace ShaderCompilerSlang;
		auto& bindingInfo = reflectionData->m_BindingInfo;
		auto& rootHierarchy = bindingInfo.m_BindingDataHierarchies[bindingInfo.m_RootHierarchyID];

		castl::deque<HierarchyBound> hierarchies;
		for (auto hierarchyID : rootHierarchy.m_SubBindingHierarchies)
		{
			auto findRootShaderStruct = [&](cacore::NameHash const& inName)->D3D2ShaderStruct const*
			{
				auto found = resoureBindings.find(inName);
				if (found != resoureBindings.end())
				{
					return found->second;
				}
				return nullptr;
			};
			auto& itrHierarchy = bindingInfo.m_BindingDataHierarchies[hierarchyID];
			hierarchies.push_back(HierarchyBound{ &itrHierarchy , findRootShaderStruct(itrHierarchy.m_Name), 0 });
		}
		while (!hierarchies.empty())
		{
			HierarchyBound bounds = hierarchies.front();
			hierarchies.pop_front();
			if (bounds.pHierarchy != nullptr && bounds.pStruct != nullptr)
			{
				auto& itrHierarchy = *bounds.pHierarchy;
				auto& itrStruct = *bounds.pStruct;
				uint32_t offset = bounds.offset;
				auto findShaderStructsFromParent = [&](cacore::NameHash const& inName)
					->castl::vector<castl::shared_ptr<ShaderStruct>> const*
				{
					auto found = itrStruct.GetSubStructs().find(inName);
					if (found != itrStruct.GetSubStructs().end())
					{
						return &found->second;
					}
					return nullptr;
				};
				for (auto subHierarchyID : itrHierarchy.m_SubBindingHierarchies)
				{
					auto& subHierarchy = bindingInfo.m_BindingDataHierarchies[subHierarchyID];
					castl::vector<castl::shared_ptr<ShaderStruct>> const* pStructs = findShaderStructsFromParent(subHierarchy.m_Name);
					auto tryGetpStruct = [&](uint32_t id) ->D3D2ShaderStruct const*
					{
						if (pStructs == nullptr)
							return nullptr;
						castl::vector<castl::shared_ptr<ShaderStruct>> const& structs = *pStructs;
						if (structs.size() <= id)
							return nullptr;
						return static_cast<D3D2ShaderStruct const*>(structs[id].get());
					};
					for (uint32_t id = 0; id < subHierarchy.m_ElementCount; ++id)
					{
						uint32_t resolvedOffset = id + offset * subHierarchy.m_ElementCount;
						D3D2ShaderStruct const* itrSubStruct = tryGetpStruct(id);
						hierarchies.push_back(HierarchyBound{ &subHierarchy , itrSubStruct, resolvedOffset });
					}
				}
			}
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


		ReflectResourceBindings(p_ReflectionData, resourceSet.resourceDic, [&](HierarchyBound const& hierarchyBound)
		{
			CA_ASSERT_BREAK(hierarchyBound.pHierarchy != nullptr, "Invalid Hierarchy");
			CA_ASSERT_BREAK(hierarchyBound.pStruct != nullptr, "Struct Not Bound {}", hierarchyBound.pHierarchy->m_Name);

			auto& hierarchy = *hierarchyBound.pHierarchy;
			auto pStruct = hierarchyBound.pStruct;

			for (uint32_t spaceID = 0; spaceID < spaceInfos.size(); ++spaceID)
			{
				//Collect Uniform Buffer
				if (hierarchy.m_SelfUniformBufferID != -1
					&& hierarchy.m_SelfUniformSpaceID == spaceID)
				{
					CA_ASSERT_BREAK(hierarchy.m_SelfUniformSpaceID != -1, "invalid uniform space id: {}", hierarchy.m_SelfUniformSpaceID);
					auto uniformBufferID = hierarchy.m_SelfUniformSpaceID;
					auto& spaceResourceInfo = m_GPUResourceSpaceInfos[spaceID];
					auto& cbufferInfo = spaceResourceInfo.cbufferInfos[uniformBufferID];
					cbufferInfo.bindingID = hierarchyBound.offset + hierarchy.m_SelfUniformBufferID;
					cbufferInfo.pCBufferStruct = pStruct;
					cbufferInfo.cbufferHandle = cbufferManager.GetConstantBufferHandle(pStruct);
				}

				//Collect Resources
				if (!hierarchy.m_Bindings.empty())
				{
					auto& bufferHandles = pStruct->GetBufferHandles();
					auto& imageHandles = pStruct->GetImageHandles();
					auto& samplerDescs = pStruct->GetSamplerDescriptors();

					for (auto& binding : hierarchy.m_Bindings)
					{
						if (binding.m_BindingSpace != spaceID)
							continue;
						auto bindingID = hierarchyBound.offset + binding.m_BindingID;
						auto& spaceResourceInfo = m_GPUResourceSpaceInfos[spaceID];
						switch (binding.m_ResourceType)
						{
							case ShaderCompilerSlang::EShaderResourceType::eTexture:
							case ShaderCompilerSlang::EShaderResourceType::eRWTexture:
							{
								auto found = imageHandles.find(binding.m_Name);
								CA_ASSERT_BREAK(found != imageHandles.end(), "Texture Not Found:{}", binding.m_Name);
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
								CA_ASSERT_BREAK(found != bufferHandles.end(), "Buffer Not Found:{}", binding.m_Name);
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
								CA_ASSERT_BREAK(found != samplerDescs.end(), "Sampler Not Found:{}", binding.m_Name);
								{
									auto& samplerList = found->second;
									SamplerBindingInfo samplerInfo{};
									samplerInfo.bindingID = bindingID;
									samplerInfo.samplerDescriptors = samplerList;
									spaceResourceInfo.samplerInfos.push_back(samplerInfo);
								}
							}
						}
					}
				}
			}
		});
	}

	void GPUResourceBindingInstance::Init1(ShaderResourceSet const& resourceSet
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
							samplerInfo.samplerDescriptors = samplerList;
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

	void GPUResourceBindingInstance::BuildDescriptors(D3D12GraphLocalResourceManager& resourceManager
		, GPUDescriptorHeap& gpuDescriptorHeap
		, GPUDescriptorHeap& samplerDescriptorHeap)
	{
		castl::vector<D3D12_ROOT_PARAMETER1> rootParameters;
		for (size_t spaceID = 0; spaceID < m_GPUResourceSpaceInfos.size(); ++spaceID)
		{
			auto& spaceInfo = m_GPUResourceSpaceInfos[spaceID];
			//Record Descriptor Table
			{
				std::vector<D3D12_DESCRIPTOR_RANGE1>& descriptors = spaceInfo.descTable;
				descriptors.clear();
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
				uint32_t samplerCount = 0;
				for (auto& samplerInfo : spaceInfo.samplerInfos)
				{
					CD3DX12_DESCRIPTOR_RANGE1 range;
					range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
						, samplerInfo.samplerDescriptors.size()
						, samplerInfo.bindingID);
					descriptors.push_back(range);
					samplerCount += samplerInfo.samplerDescriptors.size();
				}
				spaceInfo.descriptorAllocation = gpuDescriptorHeap.AllocDescriptorChunk(descriptorCount);
				spaceInfo.samplerAllocation = samplerDescriptorHeap.AllocDescriptorChunk(samplerCount);
				CD3DX12_ROOT_PARAMETER1 newParameter;
				newParameter.InitAsDescriptorTable(descriptors.size(), descriptors.data());
				rootParameters.push_back(newParameter);
			}
			{
				uint32_t descriptorID = 0;
				for (auto& cbuffer : spaceInfo.cbufferInfos)
				{
					//Copy CPU Desc To GPU DescHeap
					auto pResource = resourceManager.GetBufferResource(cbuffer.cbufferHandle);
					CA_ASSERT_BREAK(pResource != nullptr, "CBuffer {} Resource Not Found", cbuffer.cbufferHandle.GetName());
					GetDevice()->CopyDescriptorsSimple(1, spaceInfo.descriptorAllocation.Slice(descriptorID).CPUHandle()
						, pResource->cbv.CPUHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					descriptorID++;
				}
				for (auto& bufferInfo : spaceInfo.bufferInfos)
				{
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
				uint32_t samplerID = 0;
				for (auto& samplerInfo : spaceInfo.samplerInfos)
				{
					for (auto& samplerDesc : samplerInfo.samplerDescriptors)
					{
						auto descHandle = GetApp()->GetSamplerManager().GetCPUHandle(samplerDesc);
						GetDevice()->CopyDescriptorsSimple(1, spaceInfo.samplerAllocation.Slice(samplerID).CPUHandle()
							, descHandle, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
						samplerID++;
					}
				}
			}
			
		}

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
		rootSigDesc.Init_1_1(rootParameters.size(), rootParameters.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		// 编译root signature 描述结构
		ThrowIfFailed(D3D12SerializeVersionedRootSignature(&rootSigDesc, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
		if (errorBlob != nullptr)
		{
			CA_LOG_ERR_BREAK("{}", (char*)errorBlob->GetBufferPointer());
		}
		ThrowIfFailed(GetDevice()->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(m_RootSignature.GetAddressOf())));
	}

}