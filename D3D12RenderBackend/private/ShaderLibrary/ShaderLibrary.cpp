#include "ShaderLibrary.h"

namespace graphics_backend
{
	ShaderFileInfo const* ShaderLibrary::GetShaderFileInfo(cacore::PathHash const& path) const
	{
		auto it = m_ShaderFiles.find(path);
		if (it == m_ShaderFiles.end())
		{
			CA_LOG_ERR_BREAK("Unfound Shader File Info: {}", path);
			return nullptr;
		}
		return &it->second;
	}
	ShaderCode const* ShaderLibrary::GetShaderCode(cahash::sha256_hash::result_type const& shaHash) const
	{
		auto it = m_ShaderPrograms.find(shaHash);
		if (it == m_ShaderPrograms.end())
		{
			CA_LOG_ERR_BREAK("Unfound Shader Code");
			return nullptr;
		}
		return &it->second;
	}
	EShaderTypeFlags ShaderFileInfo::GetShaderStageUsage(uint32_t usageMask) const
	{
		EShaderTypeFlags result = 0;
		for (uint32_t id = 0; id < entryPointToShaderProgram.size(); ++id)
		{
			if (usageMask & id)
			{
				result &= ECompileShaderTypeToMask(entryPointToShaderProgram[id].shaderType);
			}
		}
		return result;
	}
}