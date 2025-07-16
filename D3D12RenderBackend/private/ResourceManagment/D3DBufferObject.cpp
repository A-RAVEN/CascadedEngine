#include <ResourceManagment/D3DBufferObject.h>
#include <RenderBackend_D3D12.h>

namespace graphics_backend
{
	D3DBufferObject::D3DBufferObject(RenderBackend_D3D12* app) : D3D12SubobjectBase(app), m_Resource(app)
	{
		m_LastResourceState = ResourceState::InitializedState();
	}

	GPUBufferDescriptor const& D3DBufferObject::GetDescriptor() const
	{
		return m_Descriptor;
	}

	void D3DBufferObject::SetName(castl::string const& name)
	{
		m_Name = name;
	}

	castl::string const& D3DBufferObject::GetName() const
	{
		return m_Name;
	}

	void D3DBufferObject::SetGPUResource(GPUResource&& resource)
	{
		m_Resource = castl::move(resource);
	}

	void D3DBufferObject::SetDescriptor(GPUBufferDescriptor const& desc)
	{
		m_Descriptor = desc;
	}
	DescriptorAllocation const& D3DBufferObject::EnsureResourceView(EResourceViewType viewType)
	{
		return m_CachedResourceViews.EnsureResourceView_NoLock(viewType
			, GetApp()
			, m_Resource.GetResource()
			, GetApp()->GetCommonDescriptorAllocatorSet()
			, m_Descriptor);
	}
}