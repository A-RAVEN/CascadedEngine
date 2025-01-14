#include "ShaderLibrary.h"
#include <RenderBackend_D3D12.h>

namespace graphics_backend
{
	void D3D12ShaderResourceRW::Init(ShaderInfo const& shaderInfo)
	{
		p_ShaderInfo = &shaderInfo;
	}

	void D3D12ShaderResourceRW::Release()
	{
		m_ShaderByteCode->Release();
		m_ShaderByteCode = nullptr;
	}

	void D3D12ShaderResourceRW::Serialize(ca_io::WBatch* inWriter)
	{
		cacore::batch_serialize(inWriter , *p_ShaderInfo);
		inWriter->Write(m_ShaderByteCode->GetBufferSize());
		inWriter->Write(m_ShaderByteCode->GetBufferSize(), m_ShaderByteCode->GetBufferPointer());
		inWriter->SubmitAndWait();
	}

	void D3D12ShaderResourceRW::Deserialize(ca_io::IOBatch* inReader)
	{
		cacore::batch_deserialize(inReader, *p_ShaderInfo);
		uint32_t size;
		inReader->Read(size);
		inReader->SubmitAndWait();
		m_ShaderByteCode = nullptr;
		D3DCreateBlob(size, m_ShaderByteCode.GetAddressOf());
		inReader->Read(size, m_ShaderByteCode->GetBufferPointer());
		inReader->SubmitAndWait();
	}

	castl::shared_ptr<D3D12ShaderResourceRW> D3D12ShaderLibrary::GetOrLoadShader(cacore::HashObj<ShaderInfo> const& inShaderInfo)
	{
		castl::shared_ptr<D3D12ShaderResourceRW> result = GetApp()
			->GetResourceManager()
			->GetOrLoadResource<D3D12ShaderResourceRW>(inShaderInfo.Get().path, [&]()
			{
				auto pShader = new D3D12ShaderResourceRW(GetApp());
				pShader->Init(inShaderInfo.Get());
				return pShader;
			}, [&](resource_management::IResource* pShader)
			{
				D3D12ShaderResourceRW* resource = dynamic_cast<D3D12ShaderResourceRW*>(pShader);
				resource->Release();
				delete resource;
			});
		return result;
	}
}