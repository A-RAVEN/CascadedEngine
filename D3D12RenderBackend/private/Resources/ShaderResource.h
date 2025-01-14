#pragma once
#include <CAResource/IResource.h>
namespace graphics_backend
{

	struct ShaderSourceKey
	{
		castl::string pathToFile;
		castl::string entryPoint;
		auto operator<=>(const ShaderSourceKey&) const = default;
	};

	class ShaderLibrary : public resource_management::IResource
	{
	public:
		castl::unordered_map<ShaderSourceKey, castl::shared_ptr<ShaderResource>> m_ShaderResources;
	};

	class ShaderResource : public resource_management::IResource
	{

	};
}