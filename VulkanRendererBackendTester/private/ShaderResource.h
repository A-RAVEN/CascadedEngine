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

namespace ShaderCompiler
{
	constexpr auto serialize(auto& archive, TextureParam& param)
	{
		return archive(param.name, param.set, param.binding
			, param.type, param.subpassInputAttachmentID);
	}

	constexpr auto serialize(auto& archive, BufferParam& param)
	{
		return archive(param.name, param.set, param.binding
			, param.type, param.blockSize);
	}

	constexpr auto serialize(auto& archive, ConstantBufferParam& param)
	{
		return archive(param.name, param.set, param.binding
			, param.blockSize, param.numericParams);
	}

	constexpr auto serialize(auto& archive, SamplerParam& param)
	{
		return archive(param.name, param.set, param.binding
			, param.type);
	}
}

namespace resource_management
{
	using namespace library_loader;
	using namespace ShaderCompiler;
	class ShaderResrouce : public IResource
	{
	public:
		friend zpp::bits::access;
		using serialize = zpp::bits::members<4>;
		virtual void Serialzie(std::vector<std::byte>& out) override;
		virtual void Deserialzie(std::vector<std::byte>& in) override;
		virtual void Load() override;
		TestShaderProvider m_VertexShaderProvider;
		ShaderParams m_VertexShaderParams;
		TestShaderProvider m_FragmentShaderProvider;
		ShaderParams m_FragmentShaderParams;

		ShaderBindingBuilder m_VertexBindingBuilder;
		ShaderBindingBuilder m_FragmentBindingBuilder;
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