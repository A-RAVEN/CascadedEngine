#pragma once
#include <D3D12Includes.h>
#include <CASTL/CAString.h>
#include <ShaderProvider.h>
#include <CACore/CASharedDic.h>

namespace graphics_backend
{
	struct ShaderMetaData
	{
		castl::string entryPoint;
	};

	struct ShaderSourceKey
	{
		castl::string pathToFile;
		castl::string entryPoint;

	};

	class D3D12ShaderResource
	{
	public:
		void Init(ShaderInfoHandle const&);
	private:
		ShaderInfoHandle const* m_InfoHandle;
		ComPtr<ID3DBlob> m_ShaderByteCode;
	};

	class D3D12ShaderLibrary
	{
	public:
		castl::shared_ptr<D3D12ShaderResource> GetOrLoadShader(cacore::HashObj<ShaderInfoHandle> const& inShaderInfo);
	private:
		castl::shared_dic<cacore::HashObj<ShaderInfoHandle>, castl::shared_ptr<D3D12ShaderResource>> m_ShaderSourceCache;
	};
}