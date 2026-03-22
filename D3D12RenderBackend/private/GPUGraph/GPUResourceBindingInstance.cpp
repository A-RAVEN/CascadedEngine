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
			GPUBufferDescriptor desc = GPUBufferDescriptor::Create(1, castl::alignto<uint32_t>(pStructData->m_StructUniforms.m_Stride, 256));
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

	void GPUConstantBufferManager::Clear()
	{
		m_ConstantBufferHandles.clear();
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

		//No Need Do This
		auto& bindingInfo = reflectionData.m_BindingInfo;
		auto& rootHierarchy = bindingInfo.m_BindingDataHierarchies[bindingInfo.m_RootHierarchyID];

		for (uint32_t hierarchyID : rootHierarchy.m_SubBindingHierarchies)
		{
			auto& hierarchy = bindingInfo.m_BindingDataHierarchies[hierarchyID];
			auto sourceStruct = findShaderStructOfName(hierarchy.m_Name);
			CA_ASSERT_BREAK(sourceStruct != nullptr, "Root Struct [{}] Not Found", hierarchy.m_Name);
			resourceDic.insert(castl::make_pair(hierarchy.m_Name, sourceStruct));
		}
	}


	struct HierarchyBound
	{
		ShaderCompilerSlang::ShaderBindingHierarchy const* pHierarchy;
		D3D2ShaderStruct const* pStruct;
		uint32_t offset;
	};

	struct BindingPair
	{
		StructBindingInfos const* pBindingInfo;
		D3D2ShaderStruct const* pStruct;
		uint32_t bindingOffset;
	};

	void ReflectResourceBindings(ShaderResourceBindingInfo const& bindingData
		, castl::unordered_map<cacore::NameHash, D3D2ShaderStruct const*> const& resoureBindings
		, castl::function<void(BindingPair const&)> hierarchyBoundCallback)
	{
		using namespace ShaderCompilerSlang;
		castl::deque<BindingPair> hierarchies;

		{
			auto& rootBinding = bindingData.structBindingInfos[0];

			for (uint32_t id = 0; id < rootBinding.subStructCount; ++id)
			{
				auto findRootShaderStruct = [&](cacore::NameHash const& inName)->D3D2ShaderStruct const*
				{
					auto found = resoureBindings.find(inName);
					if (found != resoureBindings.end())
					{
						return found->second;
					}
					CA_LOG_ERR_BREAK("Root Shader Struct [{}] Not Found", inName);
					return nullptr;
				};
				uint32_t subBindingID = rootBinding.subStructOffset + id;
				auto& subBindingInfo = bindingData.structBindingInfos[subBindingID];

				hierarchies.push_back(BindingPair{ &subBindingInfo , findRootShaderStruct(subBindingInfo.structBindingName), 0 });
			}
		}

		while (!hierarchies.empty())
		{
			BindingPair bindingPair = hierarchies.front();
			hierarchyBoundCallback(bindingPair);
			hierarchies.pop_front();
			if (bindingPair.pBindingInfo != nullptr && bindingPair.pStruct != nullptr)
			{
				auto& itrHierarchy = *bindingPair.pBindingInfo;
				auto& itrStruct = *bindingPair.pStruct;
				uint32_t offset = bindingPair.bindingOffset;
				auto findShaderStructsFromParent = [&](cacore::NameHash const& inName)
					->castl::vector<castl::shared_ptr<D3D2ShaderStruct>> const*
				{
					auto found = itrStruct.GetSubStructs().find(inName);
					if (found != itrStruct.GetSubStructs().end())
					{
						return &found->second;
					}
					CA_LOG_ERR_BREAK("Shader Struct [{}] Not Found in Parent Struct [{}][type {}]"
						, inName
						, itrHierarchy.structBindingName
						, itrStruct.GetStructTypeName());
					return nullptr;
				};
				for (uint32_t id = 0; id < itrHierarchy.subStructCount; ++id)
				{
					uint32_t subBindingID = itrHierarchy.subStructOffset + id;
					auto& subHierarchy = bindingData.structBindingInfos[subBindingID];
					castl::vector<castl::shared_ptr<D3D2ShaderStruct>> const* pStructs = findShaderStructsFromParent(subHierarchy.structBindingName);
					auto tryGetpStruct = [&](uint32_t id) ->D3D2ShaderStruct const*
					{
						if (pStructs == nullptr)
							return nullptr;
						castl::vector<castl::shared_ptr<D3D2ShaderStruct>> const& structs = *pStructs;
						if (structs.size() <= id)
							return nullptr;
						return static_cast<D3D2ShaderStruct const*>(structs[id].get());
					};
					for (uint32_t id = 0; id < subHierarchy.elementCount; ++id)
					{
						uint32_t resolvedOffset = id + offset * subHierarchy.elementCount;
						D3D2ShaderStruct const* itrSubStruct = tryGetpStruct(id);
						hierarchies.push_back(BindingPair{ &subHierarchy , itrSubStruct, resolvedOffset });
					}
				}
			}
		}
	}

	void GPUResourceBindingInstance::Init(ShaderResourceSet const& resourceSet)
	{
		pShaderFileInfo = GetApp()->GetShaderFileInfo(resourceSet.shaderInfo);
		auto& shaderBindingInfo = pShaderFileInfo->shaderBindingInfo;

		auto& outCBufferBindings = m_GPUResourceBindingInfos.cbufferBindings;
		auto& outImageBindings = m_GPUResourceBindingInfos.imageBindings;
		auto& outBufferBindings = m_GPUResourceBindingInfos.bufferBindings;
		auto& outSamplerBindings = m_GPUResourceBindingInfos.samplerBindings;

		ReflectResourceBindings(shaderBindingInfo, resourceSet.resourceDic, [&](BindingPair const& hierarchyBound)
		{
			CA_ASSERT_BREAK(hierarchyBound.pBindingInfo != nullptr, "Invalid Hierarchy");
			CA_ASSERT_BREAK(hierarchyBound.pStruct != nullptr, "Struct Not Bound {}", hierarchyBound.pBindingInfo->structBindingName);

			auto& structBindingInfo = *hierarchyBound.pBindingInfo;
			auto pStruct = hierarchyBound.pStruct;
			uint32_t offset = hierarchyBound.bindingOffset;

			for (auto cbufferID : structBindingInfo.cbufferRefs)
			{
				auto& bindingInfo = shaderBindingInfo.cbufferInfos[cbufferID];
				CBufferBindingElement cbufferElement{};
				cbufferElement.bindingInfo = bindingInfo;
				cbufferElement.offset = offset;
				cbufferElement.pCBufferStruct = pStruct;
				cbufferElement.usingStages = pShaderFileInfo->GetShaderStageUsage(bindingInfo.usageMask);
				outCBufferBindings.push_back(cbufferElement);
			}

			auto& bufferHandles = pStruct->GetBufferHandles();
			auto& imageHandles = pStruct->GetImageHandles();
			auto& samplerDescs = pStruct->GetSamplerDescriptors();

			for (auto imageID : structBindingInfo.imageRefs)
			{
				auto& bindingInfo = shaderBindingInfo.imageInfo[imageID];
				auto found = imageHandles.find(bindingInfo.imageBindingName);
				CA_ASSERT_BREAK(found != imageHandles.end(), "Texture Not Found:{}", bindingInfo.imageBindingName);
				auto& imageList = found->second;
				ImageBindingElement imageElement{};
				imageElement.bindingInfo = bindingInfo;
				imageElement.offset = offset;
				imageElement.usingStages = pShaderFileInfo->GetShaderStageUsage(bindingInfo.usageMask);
				imageElement.resourceUsages = bindingInfo.isUAV() ? EResourceUsage::eShaderUnorderedAccess : EResourceUsage::eShaderResource;
				for (uint32_t imgID = 0; imgID < imageList.size(); ++imgID)
				{
					ImageBindingElement::ImageBinding binding;
					binding.image = imageList[imgID].first;
					binding.textureView = imageList[imgID].second;
					imageElement.bindings.push_back(binding);
				}
				outImageBindings.push_back(imageElement);
			}

			for (auto bufferID : structBindingInfo.bufferRefs)
			{
				auto& bindingInfo = shaderBindingInfo.bufferInfos[bufferID];
				auto found = bufferHandles.find(bindingInfo.bufferBindingName);
				CA_ASSERT_BREAK(found != bufferHandles.end(), "Buffer Not Found:{}", bindingInfo.bufferBindingName);
				auto& bufferList = found->second;
				BufferBindingElement bufferElement{};
				bufferElement.bindingInfo = bindingInfo;
				bufferElement.offset = offset;
				bufferElement.usingStages = pShaderFileInfo->GetShaderStageUsage(bindingInfo.usageMask);
				bufferElement.resourceUsages = bindingInfo.isUAV() ? EResourceUsage::eShaderUnorderedAccess : EResourceUsage::eShaderResource;
				for (uint32_t bufID = 0; bufID < bufferList.size(); ++bufID)
				{
					bufferElement.bindings.push_back(bufferList[bufID]);
				}
				outBufferBindings.push_back(bufferElement);
			}

			for (auto samplerID : structBindingInfo.samplerRefs)
			{
				auto& bindingInfo = shaderBindingInfo.samplerInfos[samplerID];
				auto found = samplerDescs.find(bindingInfo.samplerBindingName);
				CA_ASSERT_BREAK(found != samplerDescs.end(), "Sampler Not Found:{}", bindingInfo.samplerBindingName);
				auto& samplerList = found->second;
				SamplerBindingElement samplerElement{};
				samplerElement.bindingInfo = bindingInfo;
				samplerElement.offset = offset;
				samplerElement.usingStages = pShaderFileInfo->GetShaderStageUsage(bindingInfo.usageMask);
				for (uint32_t smpID = 0; smpID < samplerList.size(); ++smpID)
				{
					samplerElement.samplerDescriptors.push_back(samplerList[smpID]);
				}
				outSamplerBindings.push_back(samplerElement);
			}
		});
	}

	void GPUResourceBindingInstance::Release()
	{
		m_GPUResourceBindingInfos.Release();
	}

	void GPUResourceBindingInstance::BuildResources(GPUGraph const& gpuGraph
		, D3D12GraphLocalResourceManager& resourceManager
		, GPUConstantBufferManager& cbufferManager)
	{
		for (auto& imageBinding : m_GPUResourceBindingInfos.imageBindings)
		{
			for (auto& img : imageBinding.bindings)
			{
				auto* pDesc = gpuGraph.GetImageManager().GetDescriptor(img.image.GetKey());
				CA_ASSERT_BREAK(pDesc != nullptr, "Image {} Not Registered", img.image.GetName());
				resourceManager.AddTexture(img.image, *pDesc);
			}
		}
		for (auto& bufferBinding : m_GPUResourceBindingInfos.bufferBindings)
		{
			for (auto& buf : bufferBinding.bindings)
			{
				auto* pDesc = gpuGraph.GetBufferManager().GetDescriptor(buf.GetKey());
				CA_ASSERT_BREAK(pDesc != nullptr, "Buffer {} Not Registered", buf.GetName());
				resourceManager.AddBuffer(buf, *pDesc);
			}
		}
		for (auto& cbufferBinding : m_GPUResourceBindingInfos.cbufferBindings)
		{
			GPUBufferDescriptor desc = GPUBufferDescriptor::Create(1, castl::alignto<size_t>(cbufferBinding.pCBufferStruct->GetCBufferSize(), 256));
			BufferHandle cbufferHandle = cbufferManager.GetConstantBufferHandle(cbufferBinding.pCBufferStruct);
			resourceManager.AddBuffer(cbufferHandle, desc);
		}
	}

	void GPUResourceBindingInstance::IterateResourceUsages(
		castl::function<void(ImageBindingElement const&)>const& imageCallback
		, castl::function<void(BufferBindingElement const&)>const& bufferCallback
		, castl::function<void(CBufferBindingElement const&)>const& cbufferCallback) const
	{
		for (auto& imageBinding : m_GPUResourceBindingInfos.imageBindings)
		{
			imageCallback(imageBinding);
		}
		for (auto& bufferBinding : m_GPUResourceBindingInfos.bufferBindings)
		{
			bufferCallback(bufferBinding);
		}
		for (auto& cbufferBinding : m_GPUResourceBindingInfos.cbufferBindings)
		{
			cbufferCallback(cbufferBinding);
		}
	}

	void GPUResourceBindingInstance::BuildDescriptors(D3D12GraphLocalResourceManager& resourceManager
		, CPUDescriptorAllocatorSet& descriptorAllocatorsr
		, GPUDescriptorHeap& gpuDescriptorHeap
		, GPUDescriptorHeap& samplerDescriptorHeap
		, GPUConstantBufferManager& cbufferManager)
	{
		auto& shaderBindingInfo = pShaderFileInfo->shaderBindingInfo;
		m_GPUResourceBindingInfos.descriptorAllocation = gpuDescriptorHeap.AllocDescriptorChunk(shaderBindingInfo.resourceDescCount);
		m_GPUResourceBindingInfos.samplerAllocation = samplerDescriptorHeap.AllocDescriptorChunk(shaderBindingInfo.samplerDescCount);

		for (auto& imageBinding : m_GPUResourceBindingInfos.imageBindings)
		{
			bool isUAV = imageBinding.bindingInfo.isUAV();
			uint32_t descriptorID = imageBinding.bindingInfo.descTableID + imageBinding.offset;

			for (auto& img : imageBinding.bindings)
			{
				auto writingView = resourceManager.EnsureResourceView(img.image
					, isUAV ? EResourceViewType::eUAV : EResourceViewType::eSRV
					, descriptorAllocatorsr
					, img.textureView);
				GetDevice()->CopyDescriptorsSimple(1, m_GPUResourceBindingInfos.descriptorAllocation.Slice(descriptorID).CPUHandle()
					, writingView.CPUHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				++descriptorID;
			}
		}
		for (auto& bufferBinding : m_GPUResourceBindingInfos.bufferBindings)
		{
			//Copy CPU Descs To GPU DescHeap
			bool isUAV = bufferBinding.bindingInfo.isUAV();
			uint32_t descriptorID = bufferBinding.bindingInfo.descTableID + bufferBinding.offset;
			for (auto& buf : bufferBinding.bindings)
			{
				auto writingView = resourceManager.EnsureResourceView(
					buf
					, isUAV ? EResourceViewType::eUAV : EResourceViewType::eSRV
					, descriptorAllocatorsr);
				GetDevice()->CopyDescriptorsSimple(1, m_GPUResourceBindingInfos.descriptorAllocation.Slice(descriptorID).CPUHandle()
					, writingView.CPUHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				descriptorID++;
			}
		}
		for (auto& cbufferBinding : m_GPUResourceBindingInfos.cbufferBindings)
		{
			//Copy CPU Desc To GPU DescHeap
			BufferHandle cbufferHandle = cbufferManager.GetConstantBufferHandle(cbufferBinding.pCBufferStruct);
			auto writingView = resourceManager.EnsureResourceView(cbufferHandle
				, EResourceViewType::eCBV
				, descriptorAllocatorsr);
			uint32_t descriptorID = cbufferBinding.bindingInfo.descTableID + cbufferBinding.offset;
			GetDevice()->CopyDescriptorsSimple(1, m_GPUResourceBindingInfos.descriptorAllocation.Slice(descriptorID).CPUHandle()
				, writingView.CPUHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		
		for (auto& samplerBinding : m_GPUResourceBindingInfos.samplerBindings)
		{
			uint32_t descriptorID = samplerBinding.bindingInfo.descTableID + samplerBinding.offset;
			for (auto& samplerDesc : samplerBinding.samplerDescriptors)
			{
				auto cpuHandle = GetApp()->GetSamplerManager().GetCPUHandle(samplerDesc);
				GetDevice()->CopyDescriptorsSimple(1, m_GPUResourceBindingInfos.samplerAllocation.Slice(descriptorID).CPUHandle()
					, cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
				descriptorID++;
			}
		}
	}
	ShaderResourceInstanceManager::ShaderResourceInstanceManager(RenderBackend_D3D12* app):
		D3D12SubobjectBase(app)
	{
	}
	castl::shared_ptr<GPUResourceBindingInstance> ShaderResourceInstanceManager::EnsureResourceBindingInstance(ShaderResourceSet const& resourecSet)
	{
		m_ResourceInstances.get_or_create(resourecSet, [&](ShaderResourceSet const& shaderResourceSet) -> castl::shared_ptr<GPUResourceBindingInstance>
		{
			return GetApp()->NewSubObject_Shared<GPUResourceBindingInstance>(shaderResourceSet);
		});
		return castl::shared_ptr<GPUResourceBindingInstance>();
	}
}