#pragma once
#include <D3D12Includes.h>
#include <Utils/D3D12SubobjectBase.h>
#include <CACore/CASharedDic.h>

namespace graphics_backend
{
	class RootSignatureManager : D3D12SubobjectBase
	{
	public:
		RootSignatureManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {};
		ComPtr<ID3D12RootSignature> GetRootSignature(ComPtr<ID3DBlob> const& blob);
	private:
		castl::shared_dic<ComPtr<ID3DBlob>, ComPtr<ID3D12RootSignature>> m_RootSignatures;
	};
}