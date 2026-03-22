#pragma once
#include <CASTL/CAUnorderedSet.h>
#include <CAResource/IResource.h>
#include <Common.h>
#include <Compiler.h>
#include <CShaderModuleObject.h>

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

	struct ShaderSetData
	{
		castl::shared_ptr<CShaderModuleObject>  vertexShader;
		castl::shared_ptr<CShaderModuleObject>  fragmentShader;
		castl::shared_ptr<CShaderModuleObject>  computeShader;
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