#include "ShaderResourceImporter.h"
#include <CAResource/ResourceManagingSystem.h>

namespace graphics_backend
{
	void ShaderResourceImporter::ImportResource(ResourceManagingSystem* resourceManager
		, cafs::path const& sourcePath
		, cafs::path const& destPath)
	{
		if (!cafs::exists(sourcePath))
		{
			return;
		}

		for (auto& p : cafs::recursive_directory_iterator(sourcePath))
		{
			if (p.is_regular_file())
			{
				auto postfix = p.path().extension();
				if (postfix == ".slang")
				{
					auto pCompiler = m_ShaderCompilerManager->AquireShaderCompilerShared();
					pCompiler->BeginCompileTask();
					pCompiler->AddInlcudePath(sourcePath.string().c_str());
					pCompiler->AddSourceFile(p.path().string().c_str());
					pCompiler->EnableDebugInfo();
					pCompiler->SetTarget(ShaderCompilerSlang::EShaderTargetType::eDXIL);
					pCompiler->Compile();
					if (pCompiler->HasError())
					{
						CA_LOG_ERR("Shader compile failed");
					}
					else
					{
						//auto resource = resourceManager->GetOrNewResource<ShaderRes>(castl::to_ca(outPathWithExt.string()));
						auto compileResults = pCompiler->GetResults();
						//resource->m_UniqueName = outPath;
					}
					pCompiler->EndCompileTask();
				}
			}
		}
	}
}