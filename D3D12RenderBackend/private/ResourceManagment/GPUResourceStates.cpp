#include "GPUResourceStates.h"
#include <codecvt>
#include <RenderBackend_D3D12.h>
#include <Utils/InterfaceTranslation.h>
#include <ResourceManagment/D3DImageObject.h>
#include <ResourceManagment/D3DBufferObject.h>

namespace graphics_backend
{
	DescriptorAllocation const& TextureResourceViews::GetResourceView(EResourceViewType viewType
		, GPUTextureView const& textureView) const
	{
		auto views = resourceViews.find(textureView);
		CA_ASSERT_BREAK(views != resourceViews.end(), "Resource View Not Found");
		switch (viewType)
		{
		case EResourceViewType::eSRV:
			return views->second.srv;
		case EResourceViewType::eUAV:
			return views->second.uav;
		case EResourceViewType::eRTV:
			return views->second.rtv;
		case EResourceViewType::eDSV:
			return views->second.dsv;
		default:
			CA_LOG_ERR_BREAK("Incompatible Resource View {} For Texture", (int)viewType);
			return {};
		}
	}
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
		, p_AliasedAllocator(nullptr)
	{
	}

	void D3D12GraphLocalResourceManager::SetAllocator(AliasedMemoryAllocator* allocator)
	{
		p_AliasedAllocator = allocator;
	}

	void D3D12GraphLocalResourceManager::AddTexture(ImageHandle const& imageHandle
		, GPUTextureDescriptor const& resourceDesc)
	{
		auto& resouceData = imageHandleToResource[imageHandle];
		resouceData.resourceDesc = resourceDesc;
	}

	void D3D12GraphLocalResourceManager::AddTexture(ImageHandle const& imageHandle
		, GPUTextureDescriptor const& resourceDesc
		, GPUTextureView const& textureView)
	{
		auto& resouceData = imageHandleToResource[imageHandle];
		resouceData.resourceDesc = resourceDesc;
		//resouceData.resourceViews.insert(castl::make_pair(textureView, TextureResourceAllocationInfo::ResourceViews{}));
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
		CA_ASSERT_BREAK(p_AliasedAllocator != nullptr, "Null Aliased Allocator");
		struct ResourceAllocationPasses
		{
			std::vector<ImageHandle> newImagesOnThisPass;
			std::vector<ImageHandle> releasedImagesAfterThisPass;
			std::vector<BufferHandle> newBuffersOnThisPass;
			std::vector<BufferHandle> releasedBuffersAfterThisPass;
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
				allocationPasses[lifeTime.head()].newImagesOnThisPass.push_back(img);
				allocationPasses[lifeTime.end()].releasedImagesAfterThisPass.push_back(img);
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
				allocationPasses[lifeTime.head()].newBuffersOnThisPass.push_back(buf);
				allocationPasses[lifeTime.end()].releasedBuffersAfterThisPass.push_back(buf);
			}
		}

		for (auto& cbufferPair : cbufferLifetimes)
		{
			D3D2ShaderStruct const* pStruct = cbufferPair.first;
			ResourceUsageRange const& lifeTime = cbufferPair.second;
			BufferHandle handle = constantBufferManager.GetConstantBufferHandle(pStruct);
			auto& resource = bufferHandleToResource[handle];
			resource.usages = EResourceUsage::eConstantBuffer | EResourceUsage::eCopy;
			allocationPasses[lifeTime.head()].newBuffersOnThisPass.push_back(handle);
			allocationPasses[lifeTime.end()].releasedBuffersAfterThisPass.push_back(handle);
		}

		for (auto& allocationPass : allocationPasses)
		{
			for (auto& image : allocationPass.newImagesOnThisPass)
			{
				auto& resource = imageHandleToResource[image];
				resource.gpuResource = p_AliasedAllocator->AllocateGPUResource(
					GetResourceDescFromTextureDescriptor(resource.resourceDesc, resource.usages)
					, D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_DEFAULT
					, D3D12_RESOURCE_STATE_COMMON);
			}

			for (auto& buffer : allocationPass.newBuffersOnThisPass)
			{
				auto& resource = bufferHandleToResource[buffer];
				resource.gpuResource = p_AliasedAllocator->AllocateGPUResource(
					GetResourceDescFromGPUBufferDescriptor(resource.resourceDesc, resource.usages)
					, D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_DEFAULT
					, D3D12_RESOURCE_STATE_COMMON);
			}

			for (auto& image : allocationPass.releasedImagesAfterThisPass)
			{
				auto& resource = imageHandleToResource[image];
				resource.gpuResource.FreeVirtualMemmories();
			}

			for (auto& buffer : allocationPass.releasedBuffersAfterThisPass)
			{
				auto& resource = bufferHandleToResource[buffer];
				resource.gpuResource.FreeVirtualMemmories();
			}
		}
	}

	void D3D12GraphLocalResourceManager::CommitAliasedResources()
	{
		CA_ASSERT_BREAK(p_AliasedAllocator != nullptr, "Null Aliased Allocator");
		p_AliasedAllocator->CommitAllocations();
		for (auto& pair : imageHandleToResource)
		{
			castl::wstring_convert<castl::codecvt_utf8<wchar_t>> converter;
			auto&& [imageHandle, resourceData] = pair;
			switch (imageHandle.GetType())
			{
			case ImageHandle::ImageType::Internal:
			{
				if (resourceData.gpuResource.IsValid())
				{
					resourceData.pResource = resourceData.gpuResource.GetResource();
					resourceData.pResource->SetName(converter.from_bytes(pair.first.GetName().data()).c_str());
				}
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
				if (resourceData.gpuResource.IsValid())
				{
					resourceData.pResource = resourceData.gpuResource.GetResource();
					resourceData.pResource->SetName(converter.from_bytes(pair.first.GetName().data()).c_str());
				}
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

	void D3D12GraphLocalResourceManager::Reset()
	{
		p_AliasedAllocator = nullptr;
		imageHandleToResource.clear();
		bufferHandleToResource.clear();
	}

	DescriptorAllocation const& D3D12GraphLocalResourceManager::EnsureResourceView(ImageHandle const& imageHandle, EResourceViewType viewType, CPUDescriptorAllocatorSet& allocatorSet, GPUTextureView const& textureView)
	{
		switch (imageHandle.GetType())
		{
		case ImageHandle::ImageType::Internal:
		{
			TextureResourceAllocationInfo* imageResource = GetImageResource(imageHandle);
			return imageResource->EnsureResourceView(viewType, GetApp(), allocatorSet, textureView);
		}
		case ImageHandle::ImageType::External:
		{
			auto* imageObject = imageHandle.GetTexturePtr<D3DImageObject>();
			if (imageObject == nullptr)
			{
				CA_LOG_ERR_BREAK("Invalid Image Handle: {}", imageHandle.GetName());
				return {};
			}
			return imageObject->EnsureResourceView(viewType, textureView);
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			CA_ASSERT_BREAK(viewType == EResourceViewType::eRTV, "Backbuffer Should Be RTV");
			auto* windowContext = imageHandle.GetWindowPtr<WindowContext>();
			if (windowContext == nullptr)
			{
				CA_LOG_ERR_BREAK("Invalid Backbuffer Handle: {}", imageHandle.GetName());
				return {};
			}
			return windowContext->EnsureCurrentBackBufferRTV();
		}
		default:
			CA_LOG_ERR_BREAK("Invalid Image Handle Type: {}", (int)imageHandle.GetType());
			return {};
		}
	}

	DescriptorAllocation const& D3D12GraphLocalResourceManager::EnsureResourceView(BufferHandle const& bufferHandle, EResourceViewType viewType, CPUDescriptorAllocatorSet& allocatorSet)
	{
		switch (bufferHandle.GetType())
		{
		case BufferHandle::BufferType::Internal:
		{
			BufferResourceAllocationInfo* bufferResource = GetBufferResource(bufferHandle);
			if (bufferResource == nullptr)
			{
				CA_LOG_ERR_BREAK("Invalid Buffer Handle: {}", bufferHandle.GetName());
				return {};
			}
			return bufferResource->EnsureResourceView(viewType, GetApp(), allocatorSet);
		}
		case BufferHandle::BufferType::External:
		{
			auto* bufferObject = bufferHandle.GetBufferPtr<D3DBufferObject>();
			if (bufferObject == nullptr)
			{
				CA_LOG_ERR_BREAK("Invalid Buffer Handle: {}", bufferHandle.GetName());
				return {};
			}
			return bufferObject->EnsureResourceView(viewType);
		}
		default:
			CA_LOG_ERR_BREAK("Invalid Buffer Handle Type: {}", (int)bufferHandle.GetType());
			return {};
		}
	}

	ID3D12Resource* D3D12GraphLocalResourceManager::GetImageD3D12Resource(ImageHandle const& imageHandle) const
	{
		switch (imageHandle.GetType())
		{
		case ImageHandle::ImageType::Internal:
		{
			TextureResourceAllocationInfo const* imageResource = GetImageResource(imageHandle);
			CA_ASSERT_BREAK(imageResource != nullptr, "Invalid Internal Image Handle: {}", imageHandle.GetName());
			return imageResource->gpuResource.GetResource();
		}
		case ImageHandle::ImageType::External:
		{
			auto* imageObject = imageHandle.GetTexturePtr<D3DImageObject>();
			if (imageObject == nullptr)
			{
				CA_LOG_ERR_BREAK("Invalid Image Handle: {}", imageHandle.GetName());
				return nullptr;
			}
			return imageObject->GetGPUResource().GetResource();
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			auto* windowContext = imageHandle.GetWindowPtr<WindowContext>();
			if (windowContext == nullptr)
			{
				CA_LOG_ERR_BREAK("Invalid Backbuffer Handle: {}", imageHandle.GetName());
				return nullptr;
			}
			return windowContext->GetCurrentBackBufferResource().Get();
		}
		default:
			CA_LOG_ERR_BREAK("Invalid Image Handle Type: {}", (int)imageHandle.GetType());
			return nullptr;
		}
	}

	ID3D12Resource* D3D12GraphLocalResourceManager::GetBufferD3D12Resource(BufferHandle const& bufferHandle) const
	{
		switch (bufferHandle.GetType())
		{
		case BufferHandle::BufferType::Internal:
		{
			BufferResourceAllocationInfo const* bufferResource = GetBufferResource(bufferHandle);
			if (bufferResource == nullptr)
			{
				CA_LOG_ERR_BREAK("Invalid Internal Buffer Handle: {}", bufferHandle.GetName());
				return nullptr;
			}
			return bufferResource->gpuResource.GetResource();
		}
		case BufferHandle::BufferType::External:
		{
			auto* bufferObject = bufferHandle.GetBufferPtr<D3DBufferObject>();
			if (bufferObject == nullptr)
			{
				CA_LOG_ERR_BREAK("Invalid Buffer Handle: {}", bufferHandle.GetName());
				return nullptr;
			}
			return bufferObject->GetGPUResource().GetResource();
		}
		default:
			CA_LOG_ERR_BREAK("Invalid Buffer Handle Type: {}", (int)bufferHandle.GetType());
			return nullptr;
		}
	}

	TextureResourceInfo D3D12GraphLocalResourceManager::GetTextureResourceInfo(ImageHandle const& imageHandle) const
	{
		switch (imageHandle.GetType())
		{
		case ImageHandle::ImageType::Internal:
		{
			TextureResourceAllocationInfo const* imageResource = GetImageResource(imageHandle);
			CA_ASSERT_BREAK(imageResource != nullptr, "Invalid Internal Image Handle: {}", imageHandle.GetName());
			return TextureResourceInfo{ imageResource->gpuResource.GetResource(), imageResource->resourceDesc };
		}
		case ImageHandle::ImageType::External:
		{
			auto* imageObject = imageHandle.GetTexturePtr<D3DImageObject>();
			if (imageObject == nullptr)
			{
				CA_LOG_ERR_BREAK("Invalid Image Handle: {}", imageHandle.GetName());
				return {};
			}
			return { imageObject->GetGPUResource().GetResource(), imageObject->GetDescriptor() };
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			auto* windowContext = imageHandle.GetWindowPtr<WindowContext>();
			if (windowContext == nullptr)
			{
				CA_LOG_ERR_BREAK("Invalid Backbuffer Handle: {}", imageHandle.GetName());
				return {};
			}
			return { windowContext->GetCurrentBackBufferResource().Get(), windowContext->GetBackbufferDescriptor()};
		}
		default:
			CA_LOG_ERR_BREAK("Invalid Image Handle Type: {}", (int)imageHandle.GetType());
			return {};
		}
	}

	BufferResourceInfo D3D12GraphLocalResourceManager::GetBufferResourceInfo(BufferHandle const& bufferHandle) const
	{
		switch (bufferHandle.GetType())
		{
		case BufferHandle::BufferType::Internal:
		{
			BufferResourceAllocationInfo const* bufferResource = GetBufferResource(bufferHandle);
			if (bufferResource == nullptr)
			{
				CA_LOG_ERR_BREAK("Invalid Internal Buffer Handle: {}", bufferHandle.GetName());
				return {};
			}
			return { bufferResource->gpuResource.GetResource(), bufferResource->resourceDesc };
		}
		case BufferHandle::BufferType::External:
		{
			auto* bufferObject = bufferHandle.GetBufferPtr<D3DBufferObject>();
			if (bufferObject == nullptr)
			{
				CA_LOG_ERR_BREAK("Invalid Buffer Handle: {}", bufferHandle.GetName());
				return {};
			}
			return { bufferObject->GetGPUResource().GetResource(), bufferObject->GetDescriptor() };
		}
		default:
			CA_LOG_ERR_BREAK("Invalid Buffer Handle Type: {}", (int)bufferHandle.GetType());
			return {};
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

	TextureResourceAllocationInfo* D3D12GraphLocalResourceManager::GetImageResource(ImageHandle const& imageHandle)
	{
		auto found = imageHandleToResource.find(imageHandle);
		if (found == imageHandleToResource.end())
		{
			return nullptr;
		}
		return &found->second;
	}

	BufferResourceAllocationInfo* D3D12GraphLocalResourceManager::GetBufferResource(BufferHandle const& bufferHandle)
	{
		auto found = bufferHandleToResource.find(bufferHandle);
		if (found == bufferHandleToResource.end())
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

	DescriptorAllocation TextureResourceAllocationInfo::EnsureResourceView(EResourceViewType viewType
		, RenderBackend_D3D12* app
		, CPUDescriptorAllocatorSet& allocatorSet, GPUTextureView const& textureView)
	{
		switch (viewType)
		{
		case EResourceViewType::eSRV:
			return textureResourceViews.EnsureSRV_NoLock(app, allocatorSet, pResource, resourceDesc, textureView);
		case EResourceViewType::eUAV:
			return textureResourceViews.EnsureUAV_NoLock(app, allocatorSet, pResource, resourceDesc, textureView);
		case EResourceViewType::eRTV:
			return textureResourceViews.EnsureRTV_NoLock(app, allocatorSet, pResource, resourceDesc, textureView);
		case EResourceViewType::eDSV:
			return textureResourceViews.EnsureDSV_NoLock(app, allocatorSet, pResource, resourceDesc, textureView);
		}
	}

	DescriptorAllocation const& BufferResourceViews::EnsureResourceView_NoLock(EResourceViewType viewType
		, RenderBackend_D3D12* app
		, ID3D12Resource* pResource
		, CPUDescriptorAllocatorSet& allocatorSet
		, GPUBufferDescriptor const& resourceDesc)
	{
		switch (viewType)
		{
		case EResourceViewType::eSRV:
		{
			if (!srv.IsValid())
			{
				srv = allocatorSet.m_SRV_UAV_CBV_Allocator.AllocDescriptors(1);
				auto srvDesc = GetSRVDescFromGPUBufferDescriptor(resourceDesc);
				app->GetDevice()->CreateShaderResourceView(pResource
					, &srvDesc
					, srv.CPUHandle());
			}
			return srv;
		}
		break;
		case EResourceViewType::eUAV:
		{
			if (!uav.IsValid())
			{
				uav = allocatorSet.m_SRV_UAV_CBV_Allocator.AllocDescriptors(1);
				auto uavDesc = GetUAVDescFromGPUBufferDescriptor(resourceDesc);
				app->GetDevice()->CreateUnorderedAccessView(pResource
					, nullptr
					, &uavDesc
					, uav.CPUHandle());
			}
			return uav;
		}
		break;
		case EResourceViewType::eCBV:
		{
			if (!cbv.IsValid())
			{
				cbv = allocatorSet.m_SRV_UAV_CBV_Allocator.AllocDescriptors(1);
				auto cbufferDesc = pResource->GetDesc();
				uint64_t resourceSize = resourceDesc.SizeInByte();
				CA_LOG("CBuffer Size:{}", resourceSize);
				auto cbvDesc = GetCBVDescFromGPUBufferDescriptor(pResource->GetGPUVirtualAddress(), resourceSize);
				app->GetDevice()->CreateConstantBufferView(&cbvDesc, cbv.CPUHandle());
			}
			return cbv;
		}
		break;
		default:
			CA_LOG_ERR_BREAK("Unsupported Resource View Type {} For Buffer", (int)viewType);
			return {};
		}

	}

	DescriptorAllocation BufferResourceAllocationInfo::EnsureResourceView(EResourceViewType viewType
		, RenderBackend_D3D12* app
		, CPUDescriptorAllocatorSet& allocatorSet)
	{
		return bufferResourceView.EnsureResourceView_NoLock(viewType
			, app, pResource, allocatorSet, resourceDesc);
	}

}