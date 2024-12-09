#include "D3DImageObject.h"

namespace graphics_backend
{
	D3DImageObject::D3DImageObject(RenderBackend_D3D12* app) : D3D12SubobjectBase(app), m_Resource(app)
	{}

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
}