#pragma once
#include <CAResource/ResourceImporter.h>
#include <Compiler.h>
#include <library_loader.h>
//#include <Resources/ShaderResource.h>

namespace graphics_backend
{
	using namespace library_loader;
	using namespace resource_management;
	class D3D12ShaderResourceImporter : public ResourceImporterFree
	{
	public:
		D3D12ShaderResourceImporter() : m_ShaderCompilerLoader("ShaderCompilerSlang")
		{
			m_ShaderCompilerManager = m_ShaderCompilerLoader.New();
			m_ShaderCompilerManager->InitializePoolSize(1);
		}
		virtual castl::string GetTags() const override { return "D3D12;Slang"; }
		virtual void ImportResource(ResourceManagingSystem* resourceManager
			, cafs::path const& sourcePath
			, cafs::path const& destPath) override;
		void Test();
	private:
		TModuleLoader<ShaderCompilerSlang::IShaderCompilerManager> m_ShaderCompilerLoader;
		castl::shared_ptr <ShaderCompilerSlang::IShaderCompilerManager> m_ShaderCompilerManager;
	};
}