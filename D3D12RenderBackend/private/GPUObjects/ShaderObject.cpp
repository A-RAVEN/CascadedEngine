#include "ShaderObject.h"

namespace graphics_backend
{
	void D3D12ShaderObject::Init(cacore::HashObj<ShaderSourceInfo> const& shaderCode)
	{
		// Create the shader blob
		D3DCreateBlob(shaderCode->dataLength, &m_ShaderBlob);
		memcpy(m_ShaderBlob->GetBufferPointer(), shaderCode->data, shaderCode->dataLength);
	}
	void D3D12ShaderObject::Release()
	{
		m_ShaderBlob.Reset();
	}
}