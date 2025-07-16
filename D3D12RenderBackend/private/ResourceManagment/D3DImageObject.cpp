#include "D3DImageObject.h"
#include <RenderBackend_D3D12.h>
#include <Utils/InterfaceTranslation.h>
namespace graphics_backend
{
	D3DImageObject::D3DImageObject(RenderBackend_D3D12* app) : D3D12SubobjectBase(app), m_Resource(app)
	{
		m_LastResourceState = ResourceState::InitializedState();
	}

	void D3DImageObject::Release()
	{
		castl::unique_lock lock(m_SharedMutex);
		for (auto& pair : m_CachedResourceViews.resourceViews)
		{
			auto&& [descView, data] = pair;
			data.dsv.Release();
			data.rtv.Release();
			data.srv.Release();
			data.uav.Release();
		}
	}

	GPUTextureDescriptor const& D3DImageObject::GetDescriptor() const
	{
		return m_Descriptor;
	}

	void D3DImageObject::SetName(castl::string const& name)
	{
		m_Name = name;
	}

	castl::string const& D3DImageObject::GetName() const
	{
		return m_Name;
	}

	void D3DImageObject::SetGPUResource(GPUResource&& resource)
	{
		m_Resource = std::move(resource);
	}

	void D3DImageObject::SetDescriptor(GPUTextureDescriptor const& desc)
	{
		m_Descriptor = desc;
	}

	DescriptorAllocation const& D3DImageObject::EnsureResourceView(EResourceViewType viewType, GPUTextureView const& textureView)
	{
		switch (viewType)
		{
		case EResourceViewType::eSRV:
			return EnsureSRV(textureView);
		case EResourceViewType::eUAV:
			return EnsureUAV(textureView);
		case EResourceViewType::eRTV:
			return EnsureRTV(textureView);
		case EResourceViewType::eDSV:
			return EnsureDSV(textureView);
		default:
			CA_LOG_ERR_BREAK("Incompatible Resource View {} For Texture", (int)viewType);
			return {};
		}
	}

	DescriptorAllocation const& D3DImageObject::EnsureSRV(GPUTextureView const& textureView)
	{
		return m_CachedResourceViews.EnsureSRV(GetApp(), GetApp()->GetCommonDescriptorAllocatorSet()
			, m_SharedMutex
			, m_Resource.GetResource()
			, m_Descriptor
			, textureView);
	}
	DescriptorAllocation const& D3DImageObject::EnsureUAV(GPUTextureView const& textureView)
	{
		return m_CachedResourceViews.EnsureUAV(GetApp(), GetApp()->GetCommonDescriptorAllocatorSet()
			, m_SharedMutex
			, m_Resource.GetResource()
			, m_Descriptor
			, textureView);
	}
	DescriptorAllocation const& D3DImageObject::EnsureRTV(GPUTextureView const& textureView)
	{
		return m_CachedResourceViews.EnsureRTV(GetApp(), GetApp()->GetCommonDescriptorAllocatorSet()
			, m_SharedMutex
			, m_Resource.GetResource()
			, m_Descriptor
			, textureView);
	}
	DescriptorAllocation const& D3DImageObject::EnsureDSV(GPUTextureView const& textureView)
	{
		return m_CachedResourceViews.EnsureDSV(GetApp(), GetApp()->GetCommonDescriptorAllocatorSet()
			, m_SharedMutex
			, m_Resource.GetResource()
			, m_Descriptor
			, textureView);
	}

}