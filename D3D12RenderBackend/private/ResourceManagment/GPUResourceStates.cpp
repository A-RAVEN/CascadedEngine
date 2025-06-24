#include "GPUResourceStates.h"
#include <codecvt>
#include <RenderBackend_D3D12.h>
#include <Utils/InterfaceTranslation.h>

namespace graphics_backend
{
	D3D12GraphLocalResourceManager::D3D12GraphLocalResourceManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app)
		, aliasedAllocator(app, app->GetMemoryManager().GetAllocator())
	{
	}
	void D3D12GraphLocalResourceManager::AddTexture(ImageHandle const& imageHandle
		, GPUTextureDescriptor const& resourceDesc
		, GPUTextureView const& textureView)
	{
		auto& resouceData = imageHandleToResource[imageHandle];
		resouceData.resourceDesc = resourceDesc;
		resouceData.resourceViews.insert(castl::make_pair(textureView, TextureResourceAllocationInfo::ResourceViews{}));
	}
	void D3D12GraphLocalResourceManager::AddBuffer(BufferHandle const& bufferHandle, GPUBufferDescriptor const& resourceDesc)
	{
		bufferHandleToResource[bufferHandle].resourceDesc = resourceDesc;
	}

	void D3D12GraphLocalResourceManager::AllocateAliasedResources(uint32_t resourceBatchCount
		, castl::unordered_map<ImageHandle, ResourceUsageRangeData> const& imageLifeTimes
		, castl::unordered_map<BufferHandle, ResourceUsageRangeData> const& bufferLifeTimes
		, castl::unordered_map<D3D2ShaderStruct const*, ResourceUsageRange> const& cbufferLifetimes
		, GPUConstantBufferManager& constantBufferManager
	)
	{
		struct ResourceAllocationPasses
		{
			std::vector<castl::pair<ImageHandle, ResourceUsageRangeData>> newImagesOnThisPass;
			std::vector<castl::pair<ImageHandle, ResourceUsageRangeData>> releasedImagesAfterThisPass;
			std::vector<castl::pair<BufferHandle, ResourceUsageRangeData>> newBuffersOnThisPass;
			std::vector<castl::pair<BufferHandle, ResourceUsageRangeData>> releasedBuffersAfterThisPass;
		};

		castl::vector<ResourceAllocationPasses> allocationPasses(resourceBatchCount);

		for (auto& imgPair : imageLifeTimes)
		{
			auto& img = imgPair.first;
			if (img.IsIntternal())
			{
				auto& lifeTime = imgPair.second.lifeTime;
				allocationPasses[lifeTime.head()].newImagesOnThisPass.push_back(imgPair);
				allocationPasses[lifeTime.end()].releasedImagesAfterThisPass.push_back(imgPair);
			}
		}

		for (auto& bufPair : bufferLifeTimes)
		{
			auto& buf = bufPair.first;
			if (buf.IsIntternal())
			{
				auto& lifeTime = bufPair.second.lifeTime;
				allocationPasses[lifeTime.head()].newBuffersOnThisPass.push_back(bufPair);
				allocationPasses[lifeTime.end()].releasedBuffersAfterThisPass.push_back(bufPair);
			}
		}

		for (auto& cbufferPair : cbufferLifetimes)
		{
			D3D2ShaderStruct const* pStruct = cbufferPair.first;
			ResourceUsageRange const& lifeTime = cbufferPair.second;
			BufferHandle handle = constantBufferManager.GetConstantBufferHandle(pStruct);
			castl::pair<BufferHandle, ResourceUsageRangeData> bufPair = castl::make_pair(handle, ResourceUsageRangeData{});
			allocationPasses[lifeTime.head()].newBuffersOnThisPass.push_back(bufPair);
			allocationPasses[lifeTime.end()].releasedBuffersAfterThisPass.push_back(bufPair);
		}

		for (auto& allocationPass : allocationPasses)
		{
			for (auto& imagePair : allocationPass.newImagesOnThisPass)
			{
				auto& image = imagePair.first;
				auto& resource = imageHandleToResource[image];
				resource.gpuResource = aliasedAllocator.AllocateGPUResource(
					GetResourceDescFromTextureDescriptor(resource.resourceDesc)
					, D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_DEFAULT
					, D3D12_RESOURCE_STATE_COMMON);
			}

			for (auto& bufferPair : allocationPass.newBuffersOnThisPass)
			{
				auto& buffer = bufferPair.first;
				auto& resource = bufferHandleToResource[buffer];
				resource.gpuResource = aliasedAllocator.AllocateGPUResource(
					GetResourceDescFromGPUBufferDescriptor(resource.resourceDesc)
					, D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_DEFAULT
					, D3D12_RESOURCE_STATE_COMMON);
			}

			for (auto& imagePair : allocationPass.releasedImagesAfterThisPass)
			{
				auto& image = imagePair.first;
				auto& resource = imageHandleToResource[image];
				resource.gpuResource.FreeVirtualMemmories();
			}

			for (auto& bufferPair : allocationPass.releasedBuffersAfterThisPass)
			{
				auto& buffer = bufferPair.first;
				auto& resource = bufferHandleToResource[buffer];
				resource.gpuResource.FreeVirtualMemmories();
			}
		}
		aliasedAllocator.CommitAllocations();


		for (auto& pair : imageHandleToResource)
		{
			castl::wstring_convert<castl::codecvt_utf8<wchar_t>> converter;
			auto& resourceData = pair.second;
			resourceData.gpuResource.GetResource()->SetName(converter.from_bytes(pair.first.GetName().data()).c_str());
		}
		for (auto& pair : bufferHandleToResource)
		{
			castl::wstring_convert<castl::codecvt_utf8<wchar_t>> converter;
			auto& resourceData = pair.second;
			resourceData.gpuResource.GetResource()->SetName(converter.from_bytes(pair.first.GetName().data()).c_str());
		}
	}
	
	void D3D12GraphLocalResourceManager::PrepareResourceDescriptors(CPUDescriptorAllocatorSet& descriptorAllocatorsr)
	{
		for (auto& pair : imageHandleToResource)
		{
			auto& img = pair.first;
			if (img.IsIntternal())
			{
				auto& resourceData = pair.second;
				for (auto& pair : resourceData.resourceViews)
				{
					auto& resourceView = pair.second;
					auto& textureView = pair.first;
					if (resourceData.access & D3D12_BARRIER_ACCESS_SHADER_RESOURCE)
					{
						resourceView.srv = descriptorAllocatorsr.m_SRV_UAV_CBV_Allocator.AllocDescriptors(1);
						auto srvDesc = GetSRVDescFromGPUTextureDescriptor(resourceData.resourceDesc, textureView);
						GetDevice()->CreateShaderResourceView(resourceData.gpuResource.GetResource()
							, &srvDesc
							, resourceView.srv.CPUHandle());
					}
					if (resourceData.access & D3D12_BARRIER_ACCESS_UNORDERED_ACCESS)
					{
						resourceView.uav = descriptorAllocatorsr.m_SRV_UAV_CBV_Allocator.AllocDescriptors(1);
					}
					if (resourceData.access & D3D12_BARRIER_ACCESS_RENDER_TARGET)
					{
						resourceView.rtv = descriptorAllocatorsr.m_RTV_Allocator.AllocDescriptors(1);
					}
					if (resourceData.access
						& (D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE))
					{
						resourceView.dsv = descriptorAllocatorsr.m_DSV_Allocator.AllocDescriptors(1);
					}
				}
			}
		}
		for (auto& pair : bufferHandleToResource)
		{
			auto& buf = pair.first;
			if (buf.IsIntternal())
			{
				auto& resourceData = pair.second;
				if (resourceData.access & D3D12_BARRIER_ACCESS_SHADER_RESOURCE)
				{
					resourceData.srv = descriptorAllocatorsr.m_SRV_UAV_CBV_Allocator.AllocDescriptors(1);
					auto srvDesc = GetSRVDescFromGPUBufferDescriptor(resourceData.resourceDesc);
					GetDevice()->CreateShaderResourceView(resourceData.gpuResource.GetResource()
						, &srvDesc
						, resourceData.srv.CPUHandle());
				}
				if (resourceData.access & D3D12_BARRIER_ACCESS_UNORDERED_ACCESS)
				{
					resourceData.uav = descriptorAllocatorsr.m_SRV_UAV_CBV_Allocator.AllocDescriptors(1);
					auto uavDesc = GetUAVDescFromGPUBufferDescriptor(resourceData.resourceDesc);
					GetDevice()->CreateUnorderedAccessView(resourceData.gpuResource.GetResource()
						, nullptr
						, &uavDesc
						, resourceData.uav.CPUHandle());
				}
				if (resourceData.access & D3D12_BARRIER_ACCESS_CONSTANT_BUFFER)
				{
					resourceData.cbv = descriptorAllocatorsr.m_SRV_UAV_CBV_Allocator.AllocDescriptors(1);
					auto cbvDesc = GetCBVDescFromGPUBufferDescriptor(resourceData.gpuResource.GetResource()->GetGPUVirtualAddress()
						, resourceData.resourceDesc);
					GetDevice()->CreateConstantBufferView(&cbvDesc, resourceData.cbv.CPUHandle());
				}
			}
		}
	}

	TextureResourceAllocationInfo const* D3D12GraphLocalResourceManager::GetImageResource(ImageHandle const& imageHandle) const
	{
		auto found = imageHandleToResource.find(imageHandle);
		if (found == imageHandleToResource.end())
		{
			return nullptr;
		}
		return &found->second;
	}

	BufferResourceAllocationInfo const* D3D12GraphLocalResourceManager::GetBufferResource(BufferHandle const& bufferHandle) const
	{
		auto found = bufferHandleToResource.find(bufferHandle);
		if (found == bufferHandleToResource.end())
		{
			return nullptr;
		}
		return &found->second;
	}

}