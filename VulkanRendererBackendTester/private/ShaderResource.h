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
	class ShaderResrouce : public IResource, IShaderSet
	{
	public:
		friend zpp::bits::access;
		using serialize = zpp::bits::members<4>;
		virtual void Serialzie(castl::vector<uint8_t>& out) override;
		virtual void Deserialzie(castl::vector<uint8_t>& in) override;

		virtual ShaderSourceInfo GetShaderSourceInfo(ECompileShaderType compileShaderType) const override { 
			if (!m_ShaderPrograms.empty())
			{
				for (auto& prog : m_ShaderPrograms[0].programs)
				{
					if (prog.shaderType == compileShaderType)
					{
						return { prog.data.size(), prog.data.data(), prog.entryPointName };
					}
				}
			}
			return {}; 
		}
		virtual ShaderCompilerSlang::ShaderReflectionData GetShaderReflectionData() const override { 
			if (!m_ShaderPrograms.empty())
			{
				return m_ShaderPrograms[0].m_ReflectionData;
			}
			return {}; 
		}
		virtual castl::string GetUniqueName() const override { return m_UniqueName; }

		TestShaderProvider m_VertexShaderProvider;
		TestShaderProvider m_FragmentShaderProvider;
		castl::vector<ShaderCompilerSlang::ShaderCompileTargetResult> m_ShaderPrograms;
		castl::string m_UniqueName;
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