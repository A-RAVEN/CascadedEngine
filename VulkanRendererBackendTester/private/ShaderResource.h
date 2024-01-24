#pragma once
#include <GeneralResources/header/IResource.h>
#include <GeneralResources/header/ResourceImporter.h>
#include <GeneralResources/header/ResourceManagingSystem.h>
#include "TestShaderProvider.h"
#include <ShaderCompiler/header/Compiler.h>
#include <CACore/header/library_loader.h>
#include <ExternalLib/zpp_bits/zpp_bits.h>
#include <filesystem>
#include <RenderInterface/header/ShaderBindingBuilder.h>

namespace resource_management
{
	using namespace library_loader;
	using namespace ShaderCompiler;
	class ShaderResrouce : public IResource
	{
	public:
		friend zpp::bits::access;
		using serialize = zpp::bits::members<2>;
		virtual void Serialzie(std::vector<std::byte>& out) override;
		virtual void Deserialzie(std::vector<std::byte>& in) override;
		TestShaderProvider m_VertexShaderProvider;
		TestShaderProvider m_FragmentShaderProvider;
		friend class ShaderResourceLoader;
	};

	class ShaderResourceLoader : public ResourceImporter<ShaderResrouce>
	{
	public:
		ShaderResourceLoader();
		virtual std::string GetSourceFilePostfix() const override { return ".hlsl"; }
		virtual std::string GetDestFilePostfix() const override { return ".shaderbundle"; }
		virtual std::string GetTags() const override { return "TargetAPI=Vulkan"; }
		virtual void ImportResource(ResourceManagingSystem* resourceManager, std::string const& resourcePath, std::filesystem::path const& outPath) override;
		//virtual void ImportResource(void* resourceOffset, std::string const& resourcePath, std::string const& outPath) override;
	private:
		TModuleLoader<IShaderCompiler> m_ShaderCompilerLoader;
		std::shared_ptr<IShaderCompiler> m_ShaderCompiler;
	};
}