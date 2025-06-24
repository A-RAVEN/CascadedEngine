#include "RootSignatureManager.h"
#include <D3D12Debug.h>

namespace graphics_backend
{
	ComPtr<ID3D12RootSignature> RootSignatureManager::GetRootSignature(ComPtr<ID3DBlob> const& blob)
	{
		return m_RootSignatures.get_or_create(blob, [&](ComPtr<ID3DBlob> const& blb) ->ComPtr<ID3D12RootSignature>
		{
			ComPtr<ID3D12RootSignature> rootSig = nullptr;
			if (blb != nullptr)
			{
				ThrowIfFailed(GetDevice()->CreateRootSignature(
					0,
					blb->GetBufferPointer(),
					blb->GetBufferSize(),
					IID_PPV_ARGS(rootSig.GetAddressOf())));
			}
			return rootSig;
		})->second;
	}

}