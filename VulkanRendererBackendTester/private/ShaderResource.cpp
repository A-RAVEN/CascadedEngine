#include "ShaderResource.h"
#include <FileLoader.h>
#include <filesystem>
#include "SerializationLog.h"

namespace resource_management
{
	void ShaderResrouce::Serialzie(castl::vector<uint8_t>& data)
	{
		cacore::serialize(data, *this);
	}
	void ShaderResrouce::Deserialzie(castl::vector<uint8_t>& data)
	{
		cacore::deserializer<decltype(data)> deserializer(data);
		deserializer.deserialize(*this);
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
		pCompiler->Compile();
		if (pCompiler->HasError())
		{
			CA_LOG_ERR("Shader compile failed");
		}
		else
		{
			ShaderResrouce* resource = resourceManager->AllocResource<ShaderResrouce>(castl::to_ca(outPathWithExt.string()));
			resource->m_ShaderTargetResults = pCompiler->GetResults();
			resource->m_UniqueName = outPath;
		}
		pCompiler->EndCompileTask();
	}
}