#include "ShaderLibrary.h"

namespace graphics_backend
{
	ShaderFileInfo const* ShaderLibrary::GetShaderFileInfo(cacore::PathHash const& path) const
	{
		auto it = m_ShaderFiles.find(path);
		if (it == m_ShaderFiles.end())
			return nullptr;
		return &it->second;
	}
	ShaderCode const* ShaderLibrary::GetShaderCode(cahash::sha256_hash::result_type const& shaHash) const
	{
		auto it = m_ShaderPrograms.find(shaHash);
		if (it == m_ShaderPrograms.end())
			return nullptr;
		return &it->second;
	}
}