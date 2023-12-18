#pragma once
#include <GeneralResources/header/IResource.h>
#include <GeneralResources/header/ResourceImporter.h>
#include "TestShaderProvider.h"
#include <ShaderCompiler/header/Compiler.h>
#include <SharedTools/header/library_loader.h>

namespace resource_management
{
	using namespace library_loader;
	using namespace ShaderCompiler;
	class ShaderResrouce : public IResource
	{
	public:

	private:
		TestShaderProvider m_VertexShaderProvider;
		TestShaderProvider m_FragmentShaderProvider;
		friend class ShaderResourceLoader;
	};

	class ShaderResourceLoader : public ResourceImporter<ShaderResrouce>
	{
	public:
		ShaderResourceLoader();
		virtual std::string GetSourceFilePostfix() const override { return ".hlsl"; }
		virtual std::string GetTags() const override { return "TargetAPI=Vulkan"; }
		virtual void ImportResource(void* resourceOffset, std::string const& resourcePath) override;
	private:
		TModuleLoader<IShaderCompiler> m_ShaderCompilerLoader;
		std::shared_ptr<IShaderCompiler> m_ShaderCompiler;
	};
}