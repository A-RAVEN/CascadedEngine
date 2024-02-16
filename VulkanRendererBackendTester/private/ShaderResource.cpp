#include "ShaderResource.h"
#include <FileLoader.h>
#include <filesystem>
#include "SerializationLog.h"

namespace resource_management
{
	void ShaderResrouce::Serialzie(castl::vector<uint8_t>& data)
	{
		zpp::bits::out out(data);
		auto result = out(*this);
		if (failure(result)) {
			LogZPPError("serialize failed", result);
		}
	}
	void ShaderResrouce::Deserialzie(castl::vector<uint8_t>& data)
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
	void ShaderResourceLoaderSlang::ImportResource(ResourceManagingSystem* resourceManager, castl::string const& inPath, castl::string const& outPath)
	{
		std::filesystem::path outPathWithExt = castl::to_std(outPath);
		outPathWithExt.replace_extension(GetDestFilePostfix().c_str());
		std::filesystem::path resourcePath(inPath.c_str());
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
			ShaderResrouce* resource = resourceManager->AllocResource<ShaderResrouce>(castl::to_ca(outPathWithExt.string()));

			uint64_t dataSize = 0;
			auto vertData = pCompiler->GetOutputData(vertEntryPoint, dataSize);
			resource->m_VertexShaderProvider.SetUniqueName(resourcePath.stem().string() + ".vert");
			resource->m_VertexShaderProvider.SetData("spirv", "main", vertData, dataSize);

			auto fragData = pCompiler->GetOutputData(fragEntryPoint, dataSize);
			resource->m_FragmentShaderProvider.SetUniqueName(resourcePath.stem().string() + ".frag");
			resource->m_FragmentShaderProvider.SetData("spirv", "main", fragData, dataSize);
		}
		pCompiler->EndCompileTask();
	}
}