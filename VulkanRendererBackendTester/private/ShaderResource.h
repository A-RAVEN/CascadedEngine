#pragma once
#include <CAResource/IResource.h>
#include <CAResource/ResourceImporter.h>
#include <CAResource/ResourceManagingSystem.h>
#include "TestShaderProvider.h"
#include <Compiler.h>
#include <library_loader.h>
#include <zpp_bits.h>
#include <filesystem>
#include <ShaderBindingBuilder.h>

namespace ShaderCompiler
{
	/*constexpr auto serialize(auto& archive, TextureParam& param)
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
	}*/
}

namespace resource_management
{
	using namespace library_loader;
	using namespace ShaderCompiler;
	class ShaderResrouce : public IResource
	{
	public:
		friend zpp::bits::access;
		using serialize = zpp::bits::members<2>;
		virtual void Serialzie(castl::vector<uint8_t>& out) override;
		virtual void Deserialzie(castl::vector<uint8_t>& in) override;
		virtual void Load() override;
		TestShaderProvider m_VertexShaderProvider;
		//ShaderParams m_VertexShaderParams;
		TestShaderProvider m_FragmentShaderProvider;
		//ShaderParams m_FragmentShaderParams;
		friend class ShaderResourceLoader;
	};

	class ShaderResourceLoaderSlang : public ResourceImporter<ShaderResrouce>
	{
	public:
		ShaderResourceLoaderSlang();
		virtual castl::string GetSourceFilePostfix() const override { return ".slang"; }
		virtual castl::string GetDestFilePostfix() const override { return ".shaderbundle"; }
		virtual castl::string GetTags() const override { return "TargetAPI=Vulkan"; }
		virtual void ImportResource(ResourceManagingSystem* resourceManager, castl::string const& resourcePath, castl::string const& outPath) override;
	private:
		TModuleLoader<ShaderCompilerSlang::IShaderCompilerManager> m_ShaderCompilerLoader;
		castl::shared_ptr < ShaderCompilerSlang::IShaderCompilerManager> m_ShaderCompilerManager;
	};
}