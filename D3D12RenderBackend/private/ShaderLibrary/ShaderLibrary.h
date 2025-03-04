#pragma once
#include <CASTL/CAUnorderedSet.h>
#include <CAResource/IResource.h>
#include <Common.h>
#include <Compiler.h>
#include <D3D12Includes.h>

namespace graphics_backend
{
	struct ShaderSourceKey
	{
		cacore::PathHash path;
		cacore::NameHash entryPoint;
		auto operator<=>(const ShaderSourceKey&) const = default;
	};

	struct ShaderFileInfo
	{
		cacore::PathHash path;
		castl::vector<castl::pair<cacore::NameHash, cahash::sha256_hash::result_type>> entryPointToShaderProgram;
		ShaderCompilerSlang::ShaderReflectionData reflectionData;
		auto operator<=>(const ShaderFileInfo&) const = default;
	};

	struct ShaderCode
	{
		ECompileShaderType shaderType;
		castl::vector<uint8_t> data;
		castl::unordered_set<ShaderSourceKey> sourceKeys;
	};

	struct ShaderSourceInfo
	{
		ECompileShaderType shaderType;
		uint8_t const* data;
		size_t dataLength;
		auto operator<=>(const ShaderSourceInfo&) const = default;
	};

	struct ShaderSetData
	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> fragmentShader;
		ComPtr<ID3DBlob> computeShader;
		ShaderCompilerSlang::ShaderReflectionData const* reflectionData;
	};

	class ShaderLibrary : public resource_management::TResource<ShaderLibrary>
	{
	public:
		castl::unordered_map<cacore::PathHash, ShaderFileInfo> m_ShaderFiles;
		castl::unordered_map<cahash::sha256_hash::result_type, ShaderCode> m_ShaderPrograms;
		castl::unordered_map<cacore::NameHash, ShaderCompilerSlang::ShaderStructData> m_ShaderStructs;
		castl::unordered_map<cacore::PathHash, ShaderCompilerSlang::ShaderStructData> m_ShaderRootStructs;
		ShaderFileInfo const* GetShaderFileInfo(cacore::PathHash const& path) const;
		ShaderCode const* GetShaderCode(cahash::sha256_hash::result_type const& shaHash) const;
		friend struct CATypeDescriptor<ShaderLibrary>;
	};
}

CA_REFLECTION(graphics_backend::ShaderLibrary
	, m_ShaderFiles
	, m_ShaderPrograms
	, m_ShaderStructs
	, m_ShaderRootStructs);