#include "ShaderResource.h"
#include <FileLoader.h>
#include <filesystem>
#include "SerializationLog.h"


namespace resource_management
{

	void ShaderResrouce::Serialize(ca_io::WBatch* inWriter)
	{
		cacore::batch_serializer<ca_io::WBatch> serializer(inWriter);
		serializer.serialize(*this);
		serializer.finalize();
	}

	void ShaderResrouce::Deserialize(ca_io::IOBatch* data)
	{
		cacore::batch_deserializer<ca_io::IOBatch> deserializer(data);
		deserializer.deserialize(*this);
		deserializer.finalize();
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
		pCompiler->AddInlcudePath(folderPath.generic_string().c_str());
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
			auto resource = resourceManager->GetOrNewResource<ShaderResrouce>(castl::to_ca(outPathWithExt.generic_string()));
			resource->m_ShaderTargetResults = pCompiler->GetResults();
			resource->m_UniqueName = outPath;
		}
		pCompiler->EndCompileTask();
	}
}