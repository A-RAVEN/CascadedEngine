#pragma once
#include <D3D12Includes.h>
#include <Utils/D3D12SubobjectBase.h>
#include <CACore/CASharedDic.h>

namespace graphics_backend
{
	class RootSignatureManager : public D3D12SubobjectBase
	{
	public:
		RootSignatureManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {};
		// D7: real Release — clears cached root signatures. The base
		// D3D12SubobjectBase::Release() is a no-op, so without this override the cached
		// ID3D12RootSignature (device child objects) survive RenderBackend_D3D12::Release()
		// and are only released during ~RootSignatureManager member destruction — after the
		// device was nulled, causing the device to be destroyed with live child objects →
		// D3D12 debug layer reports that resources were destroyed while the device still
		// references them.
		void Release() override;
		ComPtr<ID3D12RootSignature> GetRootSignature(ComPtr<ID3DBlob> const& blob);
	private:
		castl::shared_dic<ComPtr<ID3DBlob>, ComPtr<ID3D12RootSignature>> m_RootSignatures;
	};
}