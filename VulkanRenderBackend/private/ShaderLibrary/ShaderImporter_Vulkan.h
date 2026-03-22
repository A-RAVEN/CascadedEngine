#pragma once
#include <CAResource/ResourceImporter.h>
#include <Compiler.h>
#include <library_loader.h>
//#include <Resources/ShaderResource.h>

namespace graphics_backend
{
	using namespace library_loader;
	using namespace resource_management;
	class VKShaderResourceImporter : public ResourceImporterFree
	{
	public:
		VKShaderResourceImporter() : m_ShaderCompilerLoader("ShaderCompilerSlang")
		{
			m_ShaderCompilerManager = m_ShaderCompilerLoader.New();
			m_ShaderCompilerManager->InitializePoolSize(1);
		}
		virtual castl::string GetTags() const override { return "Vulkan;Slang"; }
		virtual void ImportResource(ResourceManagingSystem* resourceManager
			, cafs::path const& sourcePath
			, cafs::path const& destPath) override;
	private:
		TModuleLoader<ShaderCompilerSlang::IShaderCompilerManager> m_ShaderCompilerLoader;
		castl::shared_ptr <ShaderCompilerSlang::IShaderCompilerManager> m_ShaderCompilerManager;
	};
}