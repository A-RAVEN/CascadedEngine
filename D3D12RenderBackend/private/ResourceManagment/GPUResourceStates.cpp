#include "GPUResourceStates.h"
#include <codecvt>
#include <RenderBackend_D3D12.h>
#include <Utils/InterfaceTranslation.h>
#include <ResourceManagment/D3DImageObject.h>
#include <ResourceManagment/D3DBufferObject.h>

namespace graphics_backend
{
	DescriptorAllocation const& TextureResourceViews::EnsureSRV_NoLock(RenderBackend_D3D12* app, CPUDescriptorAllocatorSet& allocatorSet, ID3D12Resource* pResource, GPUTextureDescriptor const& desc, GPUTextureView const& textureView)
	{
		auto& views = resourceViews[textureView];
		if (views.srv.IsValid())
			return views.srv;
		auto& allocator = allocatorSet.m_SRV_UAV_CBV_Allocator;
		views.srv = allocator.AllocDescriptors(1);

		auto srvDesc = GetSRVDescFromGPUTextureDescriptor(desc, textureView);
		app->GetDevice()->CreateShaderResourceView(pResource
			, &srvDesc
			, views.srv.CPUHandle());
		return views.srv;
	}

	DescriptorAllocation const& TextureResourceViews::EnsureUAV_NoLock(RenderBackend_D3D12* app, CPUDescriptorAllocatorSet& allocatorSet, ID3D12Resource* pResource, GPUTextureDescriptor const& desc, GPUTextureView const& textureView)
	{
		auto& views = resourceViews[textureView];
		if (views.uav.IsValid())
			return views.uav;
		auto& allocator = allocatorSet.m_SRV_UAV_CBV_Allocator;
		views.uav = allocator.AllocDescriptors(1);

		auto uavDesc = GetUAVDescFromGPUTextureDescriptor(desc, textureView);
		app->GetDevice()->CreateUnorderedAccessView(pResource
			, nullptr
			, &uavDesc
			, views.uav.CPUHandle());
		return views.uav;
	}

	DescriptorAllocation const& TextureResourceViews::EnsureRTV_NoLock(RenderBackend_D3D12* app, CPUDescriptorAllocatorSet& allocatorSet, ID3D12Resource* pResource, GPUTextureDescriptor const& desc, GPUTextureView const& textureView)
	{
		auto& views = resourceViews[textureView];
		if (views.rtv.IsValid())
			return views.rtv;
		auto& allocator = allocatorSet.m_RTV_Allocator;
		views.rtv = allocator.AllocDescriptors(1);

		auto rtvDesc = GetRTVDescFromTextureDescriptor(desc);
		app->GetDevice()->CreateRenderTargetView(pResource
			, &rtvDesc
			, views.rtv.CPUHandle());
		return views.rtv;
	}

	DescriptorAllocation const& TextureResourceViews::EnsureDSV_NoLock(RenderBackend_D3D12* app, CPUDescriptorAllocatorSet& allocatorSet, ID3D12Resource* pResource, GPUTextureDescriptor const& desc, GPUTextureView const& textureView)
	{
		auto& views = resourceViews[textureView];
		if (views.dsv.IsValid())
			return views.dsv;
		auto& allocator = allocatorSet.m_DSV_Allocator;
		views.dsv = allocator.AllocDescriptors(1);

		auto dsvDesc = GetDSVDescFromTextureDescriptor(desc);
		app->GetDevice()->CreateDepthStencilView(pResource
			, &dsvDesc
			, views.dsv.CPUHandle());
		return views.dsv;
	}

	DescriptorAllocation const& TextureResourceViews::EnsureSRV(RenderBackend_D3D12* app
		, CPUDescriptorAllocatorSet& allocatorSet
		, castl::shared_mutex& inMutex
		, ID3D12Resource* pResource
		, GPUTextureDescriptor const& desc
		, GPUTextureView const& textureView)
	{
		{
			castl::shared_lock lock(inMutex);
			auto& views = resourceViews[textureView];
			if (views.srv.IsValid())
				return views.srv;
		}
		{
			castl::unique_lock lock(inMutex);
			return EnsureSRV_NoLock(app, allocatorSet, pResource, desc, textureView);
		}
	}

	DescriptorAllocation const& TextureResourceViews::EnsureUAV(RenderBackend_D3D12* app
		, CPUDescriptorAllocatorSet& allocatorSet
		, castl::shared_mutex& inMutex
		, ID3D12Resource* pResource
		, GPUTextureDescriptor const& desc
		, GPUTextureView const& textureView)
	{
		{
			castl::shared_lock lock(inMutex);
			auto& views = resourceViews[textureView];
			if (views.uav.IsValid())
				return views.uav;
		}
		{
			castl::unique_lock lock(inMutex);
			return EnsureUAV_NoLock(app, allocatorSet, pResource, desc, textureView);
		}
	}

	DescriptorAllocation const& TextureResourceViews::EnsureRTV(RenderBackend_D3D12* app
		, CPUDescriptorAllocatorSet& allocatorSet
		, castl::shared_mutex& inMutex
		, ID3D12Resource* pResource
		, GPUTextureDescriptor const& desc
		, GPUTextureView const& textureView)
	{
		{
			castl::shared_lock lock(inMutex);
			auto& views = resourceViews[textureView];
			if (views.rtv.IsValid())
				return views.rtv;
		}
		{
			castl::unique_lock lock(inMutex);
			return EnsureRTV_NoLock(app, allocatorSet, pResource, desc, textureView);
		}
	}

	DescriptorAllocation const& TextureResourceViews::EnsureDSV(RenderBackend_D3D12* app
		, CPUDescriptorAllocatorSet& allocatorSet
		, castl::shared_mutex& inMutex
		, ID3D12Resource* pResource
		, GPUTextureDescriptor const& desc
		, GPUTextureView const& textureView)
	{
		{
			castl::shared_lock lock(inMutex);
			auto& views = resourceViews[textureView];
			if (views.dsv.IsValid())
				return views.dsv;
		}
		{
			castl::unique_lock lock(inMutex);
			return EnsureDSV_NoLock(app, allocatorSet, pResource, desc, textureView);
			
		}
	}

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
			auto& resource = imageHandleToResource[img];
			resource.usages = imgPair.second.allUsages;
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
			auto& resource = bufferHandleToResource[buf];
			resource.usages = bufPair.second.allUsages;
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
			auto&&[imageHandle, resourceData] = pair;
			switch (imageHandle.GetType())
			{
			case ImageHandle::ImageType::Internal:
			{
				resourceData.pResource = resourceData.gpuResource.GetResource();
				resourceData.pResource->SetName(converter.from_bytes(pair.first.GetName().data()).c_str());
				break;
			}
			case ImageHandle::ImageType::External:
			{
				resourceData.pResource = imageHandle.GetTexturePtr<D3DImageObject>()->GetGPUResource().GetResource();
				break;
			}
			case ImageHandle::ImageType::Backbuffer:
			{
				resourceData.pResource = imageHandle.GetWindowPtr<WindowContext>()->GetCurrentBackBufferResource().Get();
				break;
			}
			}

		}
		for (auto& pair : bufferHandleToResource)
		{
			castl::wstring_convert<castl::codecvt_utf8<wchar_t>> converter;
			auto&& [bufferHandle, resourceData] = pair;
			switch (bufferHandle.GetType())
			{
			case BufferHandle::BufferType::Internal:
			{
				resourceData.pResource = resourceData.gpuResource.GetResource();
				resourceData.pResource->SetName(converter.from_bytes(pair.first.GetName().data()).c_str());
				break;
			}
			case BufferHandle::BufferType::External:
			{
				resourceData.pResource = bufferHandle.GetBufferPtr<D3DBufferObject>()->GetGPUResource().GetResource();
				break;
			}
			}

		}
	}

	void D3D12GraphLocalResourceManager::PrepareResourceDescriptors(CPUDescriptorAllocatorSet& descriptorAllocatorsr)
	{
		for (auto& pair : imageHandleToResource)
		{
			auto& img = pair.first;
			{
				auto& resourceData = pair.second;
				for (auto& pair : resourceData.resourceViews)
				{
					auto& resourceView = pair.second;
					auto& textureView = pair.first;
					if (resourceData.usages & EResourceUsage::eShaderResource)
					{
						switch (img.GetType())
						{
						case ImageHandle::ImageType::Internal:
						{
							resourceView.srv = descriptorAllocatorsr.m_SRV_UAV_CBV_Allocator.AllocDescriptors(1);
							auto srvDesc = GetSRVDescFromGPUTextureDescriptor(resourceData.resourceDesc, textureView);
							GetDevice()->CreateShaderResourceView(resourceData.gpuResource.GetResource()
								, &srvDesc
								, resourceView.srv.CPUHandle());
							break;
						}
						case ImageHandle::ImageType::External:
						{
							resourceView.srv = img.GetTexturePtr<D3DImageObject>()->EnsureSRV(textureView);
							break;
						}
						case ImageHandle::ImageType::Backbuffer:
						{
							//Can We Sample Backbuffers?
						}
						}
	
					}
					if (resourceData.usages & EResourceUsage::eShaderUnorderedAccess)
					{
	
						switch (img.GetType())
						{
						case ImageHandle::ImageType::Internal:
						{
							resourceView.uav = descriptorAllocatorsr.m_SRV_UAV_CBV_Allocator.AllocDescriptors(1);
							auto uavDesc = GetUAVDescFromGPUTextureDescriptor(resourceData.resourceDesc, textureView);
							GetDevice()->CreateUnorderedAccessView(resourceData.gpuResource.GetResource()
								, nullptr
								, &uavDesc
								, resourceView.uav.CPUHandle());
							break;
						}
						case ImageHandle::ImageType::External:
						{
							resourceView.uav = img.GetTexturePtr<D3DImageObject>()->EnsureUAV(textureView);
							break;
						}
						case ImageHandle::ImageType::Backbuffer:
						{
							//Can We Sample Backbuffers?
							CA_LOG_ERR_BREAK("BackBuffer Not Supported For SRV");
						}
						}
					}
					if (resourceData.usages & EResourceUsage::eRenderTarget)
					{
						switch (img.GetType())
						{
						case ImageHandle::ImageType::Internal:
						{
							resourceView.rtv = descriptorAllocatorsr.m_RTV_Allocator.AllocDescriptors(1);
							auto rtvDesc = GetRTVDescFromTextureDescriptor(resourceData.resourceDesc);
							GetDevice()->CreateRenderTargetView(resourceData.gpuResource.GetResource()
								, &rtvDesc
								, resourceView.rtv.CPUHandle());
							break;
						}
						case ImageHandle::ImageType::External:
						{
							resourceView.rtv = img.GetTexturePtr<D3DImageObject>()->EnsureRTV(textureView);
							break;
						}
						case ImageHandle::ImageType::Backbuffer:
						{
							resourceView.rtv = img.GetWindowPtr<WindowContext>()->EnsureCurrentBackBufferRTV();
							break;
						}
						}
					}
					if (resourceData.usages & EResourceUsage::eDepthStencilTarget)
					{

						switch (img.GetType())
						{
						case ImageHandle::ImageType::Internal:
						{
							resourceView.dsv = descriptorAllocatorsr.m_DSV_Allocator.AllocDescriptors(1);
							auto dsvDesc = GetDSVDescFromTextureDescriptor(resourceData.resourceDesc);
							GetDevice()->CreateDepthStencilView(resourceData.gpuResource.GetResource()
								, &dsvDesc
								, resourceView.dsv.CPUHandle());
							break;
						}
						case ImageHandle::ImageType::External:
						{
							resourceView.rtv = img.GetTexturePtr<D3DImageObject>()->EnsureDSV(textureView);
							break;
						}
						case ImageHandle::ImageType::Backbuffer:
						{
							CA_LOG_ERR_BREAK("BackBuffer Not Supported For DSV");
							//resourceView.rtv = img.GetWindowPtr<WindowContext>()->EnsureCurrentBackBufferRTV();
							//break;
						}
						}
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
				if (resourceData.usages & EResourceUsage::eShaderResource)
				{
					resourceData.srv = descriptorAllocatorsr.m_SRV_UAV_CBV_Allocator.AllocDescriptors(1);
					auto srvDesc = GetSRVDescFromGPUBufferDescriptor(resourceData.resourceDesc);
					GetDevice()->CreateShaderResourceView(resourceData.gpuResource.GetResource()
						, &srvDesc
						, resourceData.srv.CPUHandle());
				}
				if (resourceData.usages & EResourceUsage::eShaderUnorderedAccess)
				{
					resourceData.uav = descriptorAllocatorsr.m_SRV_UAV_CBV_Allocator.AllocDescriptors(1);
					auto uavDesc = GetUAVDescFromGPUBufferDescriptor(resourceData.resourceDesc);
					GetDevice()->CreateUnorderedAccessView(resourceData.gpuResource.GetResource()
						, nullptr
						, &uavDesc
						, resourceData.uav.CPUHandle());
				}
				if (resourceData.usages & EResourceUsage::eConstantBuffer)
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