#include "ShaderResource.h"
#include <SharedTools/header/FileLoader.h>
#include <filesystem>
#include "SerializationLog.h"

namespace resource_management
{
	ShaderResourceLoader::ShaderResourceLoader() : m_ShaderCompilerLoader("ShaderCompiler")
	{
		m_ShaderCompiler = m_ShaderCompilerLoader.New();
	}

	void ShaderResourceLoader::ImportResource(ResourceManagingSystem* resourceManager
		, std::string const& inPath
		, std::filesystem::path const& outPath)
	{
		auto outPathWithExt = outPath;
		outPathWithExt.replace_extension(GetDestFilePostfix());
		std::filesystem::path resourcePath(inPath);
		auto fileName = resourcePath.filename();
		ShaderResrouce* resource = resourceManager->AllocResource<ShaderResrouce>(outPathWithExt.string());
		auto shaderSource = fileloading_utils::LoadStringFile(inPath);
		auto spirVResult = m_ShaderCompiler->CompileShaderSource(EShaderSourceType::eHLSL
			, fileName.string()
			, shaderSource
			, "vert"
			, ECompileShaderType::eVert
			, true, false);
		resource->m_VertexShaderProvider.SetUniqueName(resourcePath.stem().string() + ".vert");
		resource->m_VertexShaderProvider.SetData("spirv", "vert", spirVResult.data(), spirVResult.size() * sizeof(uint32_t));
		
		spirVResult = m_ShaderCompiler->CompileShaderSource(EShaderSourceType::eHLSL
			, fileName.string()
			, shaderSource
			, "frag"
			, ECompileShaderType::eFrag
			, true, false);

		
		resource->m_FragmentShaderProvider.SetUniqueName(resourcePath.stem().string() + ".frag");
		resource->m_FragmentShaderProvider.SetData("spirv", "frag", spirVResult.data(), spirVResult.size() * sizeof(uint32_t));
	}
	void ShaderResrouce::Serialzie(std::vector<std::byte>& data)
	{
		zpp::bits::out out(data);
		auto result = out(*this);
		if (failure(result)) {
			LogZPPError("serialize failed", result);
		}
	}
	void ShaderResrouce::Deserialzie(std::vector<std::byte>& data)
	{
		zpp::bits::in in(data);
		auto result = in(*this);
		if (failure(result)) {
			LogZPPError("deserialize failed", result);
		}
	}
}