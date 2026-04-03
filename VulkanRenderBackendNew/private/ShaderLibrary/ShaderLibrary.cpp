#include "ShaderLibrary.h"

namespace graphics_backend
{
	VulkanShaderFileInfo const* ShaderLibrary::GetShaderFileInfo(cacore::PathHash const& pathHash) const
	{
		auto it = m_ShaderFiles.find(pathHash);
		if (it == m_ShaderFiles.end())
		{
			CA_LOG_ERR_BREAK("Unfound Shader File Info: {}", pathHash);
			return nullptr;
		}
		return &it->second;
	}

	VulkanShaderCode const* ShaderLibrary::GetShaderCode(cahash::sha256_hash::result_type const& shaHash) const
	{
		auto it = m_ShaderPrograms.find(shaHash);
		if (it == m_ShaderPrograms.end())
		{
			CA_LOG_ERR_BREAK("Unfound Shader Code");
			return nullptr;
		}
		return &it->second;
	}

	ShaderCompilerSlang::ShaderStructData const* ShaderLibrary::GetShaderStruct(cacore::NameHash const& nameHash) const
	{
		auto it = m_ShaderStructs.find(nameHash);
		if (it == m_ShaderStructs.end())
		{
			CA_LOG_ERR_BREAK("Unfound Shader Struct: {}", nameHash);
			return nullptr;
		}
		return &it->second;
	}

	ShaderCompilerSlang::ShaderStructData const* ShaderLibrary::GetShaderRootStruct(cacore::PathHash const& pathHash) const
	{
		auto it = m_ShaderRootStructs.find(pathHash);
		if (it == m_ShaderRootStructs.end())
		{
			CA_LOG_ERR_BREAK("Unfound Shader Root Struct: {}", pathHash);
			return nullptr;
		}
		return &it->second;
	}
}
