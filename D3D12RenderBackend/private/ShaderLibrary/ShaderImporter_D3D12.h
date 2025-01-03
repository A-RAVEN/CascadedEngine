#pragma once
#include <CAResource/ResourceImporter.h>

namespace graphics_backend
{
	using namespace resource_management;
	class D3D12ShaderResourceImporter : public ResourceImporterBase
	{
	public:
		virtual castl::string GetSourceFilePostfix() const override { return ".slang"; }
		virtual castl::string GetDestFilePostfix() const override { return ".d3d12sh"; }
		virtual castl::string GetTags() const override { return "D3D12"; }
		virtual void ImportResource(ResourceManagingSystem* resourceManager
			, castl::string const& resourcePath
			, castl::string const& outPath) override;
	};
}