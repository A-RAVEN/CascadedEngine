#pragma once
#include <CAResource/ResourceImporter.h>
#include <Compiler.h>
#include <library_loader.h>
#include <Resources/ShaderResource.h>

namespace graphics_backend
{
	using namespace library_loader;
	using namespace resource_management;
	class ShaderResourceImporter : public ResourceImporterFree
	{
	public:
		ShaderResourceImporter() : m_ShaderCompilerLoader("ShaderCompilerSlang")
		{
			m_ShaderCompilerManager = m_ShaderCompilerLoader.New();
		}
		virtual castl::string GetTags() const override { return "Win32;HLSL;Slang"; }
		virtual void ImportResource(ResourceManagingSystem* resourceManager
			, cafs::path const& sourcePath
			, cafs::path const& destPath) override;
	private:
		TModuleLoader<ShaderCompilerSlang::IShaderCompilerManager> m_ShaderCompilerLoader;
		castl::shared_ptr <ShaderCompilerSlang::IShaderCompilerManager> m_ShaderCompilerManager;
	};
}