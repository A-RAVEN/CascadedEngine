#include <Utils/D3D12SubobjectBase.h>
#include <RenderBackend_D3D12.h>

namespace graphics_backend
{
	D3D12SubobjectBase::D3D12SubobjectBase(RenderBackend_D3D12* app) : pApp(app)
	{

	}

	D3D12SubobjectBase::D3D12SubobjectBase(D3D12SubobjectBase&& other) : pApp(other.pApp)
	{
	}

	ComPtr<IDXGIFactory4> D3D12SubobjectBase::GetFactory() const
	{
		return pApp->GetFactory();
	}
	ComPtr<ID3D12Device>  D3D12SubobjectBase::GetDevice() const
	{
		return pApp->GetDevice();
	}
	ComPtr<IDXGIAdapter1> D3D12SubobjectBase::GetAdapter() const
	{
		return pApp->GetAdapter();
	}
}