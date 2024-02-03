#include "ShaderResource.h"
#include <CACore/header/FileLoader.h>
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
			, false, true);
		resource->m_VertexShaderProvider.SetUniqueName(resourcePath.stem().string() + ".vert");
		resource->m_VertexShaderProvider.SetData("spirv", "vert", spirVResult.data(), spirVResult.size() * sizeof(uint32_t));
		resource->m_VertexShaderParams = m_ShaderCompiler->ExtractShaderParams(spirVResult);

	/*	std::string str;
		str.resize(spirVResult.size() * sizeof(uint32_t));
		memcpy(str.data(), spirVResult.data(), spirVResult.size() * sizeof(uint32_t));
		CA_LOG_ERR(str);*/

		spirVResult = m_ShaderCompiler->CompileShaderSource(EShaderSourceType::eHLSL
			, fileName.string()
			, shaderSource
			, "frag"
			, ECompileShaderType::eFrag
			, false, true);
		
		resource->m_FragmentShaderProvider.SetUniqueName(resourcePath.stem().string() + ".frag");
		resource->m_FragmentShaderProvider.SetData("spirv", "frag", spirVResult.data(), spirVResult.size() * sizeof(uint32_t));
		resource->m_FragmentShaderParams = m_ShaderCompiler->ExtractShaderParams(spirVResult);

	/*	str.resize(spirVResult.size() * sizeof(uint32_t));
		memcpy(str.data(), spirVResult.data(), spirVResult.size() * sizeof(uint32_t));
		CA_LOG_ERR(str);*/
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
	void ShaderResrouce::Load()
	{
	}
	ShaderResourceLoaderSlang::ShaderResourceLoaderSlang()
		: m_ShaderCompilerLoader("ShaderCompilerSlang")
	{
		m_ShaderCompilerManager = m_ShaderCompilerLoader.New();
		m_ShaderCompilerManager->InitializePoolSize(1);
	}
	void ShaderResourceLoaderSlang::ImportResource(ResourceManagingSystem* resourceManager, std::string const& inPath, std::filesystem::path const& outPath)
	{
		auto outPathWithExt = outPath;
		outPathWithExt.replace_extension(GetDestFilePostfix());
		std::filesystem::path resourcePath(inPath);
		std::filesystem::path folderPath = resourcePath;
		folderPath.remove_filename();
		auto pCompiler = m_ShaderCompilerManager->AquireShaderCompilerShared();
		pCompiler->BeginCompileTask();
		pCompiler->AddInlcudePath(folderPath.string().c_str());
		pCompiler->AddSourceFile(inPath.c_str());
		pCompiler->EnableDebugInfo();
		pCompiler->SetTarget(ShaderCompilerSlang::EShaderTargetType::eSpirV);
		int vertEntryPoint = pCompiler->AddEntryPoint("vert", ECompileShaderType::eVert);
		int fragEntryPoint = pCompiler->AddEntryPoint("frag", ECompileShaderType::eFrag);
		pCompiler->Compile();
		if (pCompiler->HasError())
		{
			CA_LOG_ERR("Shader compile failed");
		}
		else
		{
			ShaderResrouce* resource = resourceManager->AllocResource<ShaderResrouce>(outPathWithExt.string());

			uint64_t dataSize = 0;
			auto vertData = pCompiler->GetOutputData(vertEntryPoint, dataSize);
			resource->m_VertexShaderProvider.SetUniqueName(resourcePath.stem().string() + ".vert");
			resource->m_VertexShaderProvider.SetData("spirv", "main", vertData, dataSize);

			//std::string str;
			//str.resize(dataSize);
			//memcpy(str.data(), vertData, dataSize);
			//CA_LOG_ERR(str);

			auto fragData = pCompiler->GetOutputData(fragEntryPoint, dataSize);
			resource->m_FragmentShaderProvider.SetUniqueName(resourcePath.stem().string() + ".frag");
			resource->m_FragmentShaderProvider.SetData("spirv", "main", fragData, dataSize);

			//str.resize(dataSize);
			//memcpy(str.data(), fragData, dataSize);
			//CA_LOG_ERR(str);
		}
		pCompiler->EndCompileTask();
	}
}