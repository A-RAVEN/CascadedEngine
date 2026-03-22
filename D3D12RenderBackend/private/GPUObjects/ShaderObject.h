//#pragma once
//#include <D3D12Includes.h>
//#include <Utils/D3D12SubobjectBase.h>
//#include <CACore/CAHash.h>
//#include <ShaderLibrary/ShaderLibrary.h>
//#include <Utils/HashDictionary.h>
//
//namespace graphics_backend
//{
//	class D3D12ShaderObject : public D3D12SubobjectBase
//	{
//	public:
//		D3D12ShaderObject(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {}
//		void Init(cacore::HashObj<ShaderSourceInfo> const& shaderCode);
//		void Release();
//		ComPtr<ID3DBlob> const& GetShaderBlob() const {
//			return m_ShaderBlob;
//		}
//	private:
//		ComPtr<ID3DBlob> m_ShaderBlob;
//	};
//
//	using D3D12ShaderObjectDic = HashDictionary<ShaderSourceInfo, D3D12ShaderObject>;
//}