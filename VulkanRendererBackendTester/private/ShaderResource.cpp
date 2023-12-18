#include "ShaderResource.h"
#include <SharedTools/header/FileLoader.h>
#include <filesystem>

namespace resource_management
{
	ShaderResourceLoader::ShaderResourceLoader() : m_ShaderCompilerLoader("ShaderCompiler")
	{
		m_ShaderCompiler = m_ShaderCompilerLoader.New();
	}
	void ShaderResourceLoader::ImportResource(void* resourceOffset, std::string const& inPath)
	{
		std::filesystem::path resourcePath(inPath);
		auto fileName = resourcePath.filename();

		ShaderResrouce* resource = new(resourceOffset) ShaderResrouce();
		auto shaderSource = fileloading_utils::LoadStringFile(inPath);
		auto spirVResult = m_ShaderCompiler->CompileShaderSource(EShaderSourceType::eHLSL
			, fileName.string()
			, shaderSource
			, "vert"
			, ECompileShaderType::eVert
			, false, true);
		resource->m_VertexShaderProvider.SetUniqueName(fileName.string() + ".vert");
		resource->m_VertexShaderProvider.SetData("spirv", "vert", spirVResult.data(), spirVResult.size() * sizeof(uint32_t));
		
		spirVResult = m_ShaderCompiler->CompileShaderSource(EShaderSourceType::eHLSL
			, fileName.string()
			, shaderSource
			, "frag"
			, ECompileShaderType::eFrag
			, false, true);

		resource->m_FragmentShaderProvider.SetUniqueName(fileName.string() + ".frag");
		resource->m_FragmentShaderProvider.SetData("spirv", "frag", spirVResult.data(), spirVResult.size() * sizeof(uint32_t));
	}
}