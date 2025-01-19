#pragma once
#include <D3D12Includes.h>
#include <CASTL/CAString.h>
#include <ShaderProvider.h>
#include <CACore/CASharedDic.h>
#include <CAResource/IResource.h>
#include <Utils/D3D12SubobjectBase.h>

namespace graphics_backend
{

	//struct ShaderSourceKey
	//{
	//	castl::string pathToFile;
	//	castl::string entryPoint;
	//};

	class D3D12ShaderResourceRW : public resource_management::IResource, public D3D12SubobjectBase
	{
	public:
		D3D12ShaderResourceRW(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {}
		void Init(ShaderInfo const& shaderInfo);
		void Release();
		void Serialize(ca_io::WBatch* inWriter) override;
		virtual void Deserialize(ca_io::IOBatch* inReader) override;
	private:
		ShaderInfo const* p_ShaderInfo;
		ComPtr<ID3DBlob> m_ShaderByteCode;
		ShaderCompilerSlang::ShaderReflectionData m_ReflectionData;
		friend class D3D12ShaderLibrary;
	};

	class D3D12ShaderLibrary : public D3D12SubobjectBase
	{
	public:
		D3D12ShaderLibrary(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {}
		castl::shared_ptr<D3D12ShaderResourceRW> GetOrLoadShader(cacore::HashObj<ShaderInfo> const& inShaderInfo);
	private:
		castl::shared_dic<ShaderInfo, castl::shared_ptr<D3D12ShaderResourceRW>> m_ShaderSourceCache;
	};
}