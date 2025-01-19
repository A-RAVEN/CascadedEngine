#pragma once
#include <CAResource/IResource.h>
#include <CASTL/CAUnorderedSet.h>
#include <CASTL/CASharedPtr.h>
namespace graphics_backend
{

	struct ShaderSourceKey
	{
		castl::string pathToFile;
		castl::string entryPoint;
		auto operator<=>(const ShaderSourceKey&) const = default;
	};

	struct ShaderCode
	{
		ECompileShaderType shaderType;
		castl::vector<uint8_t> data;
		castl::unordered_set<ShaderSourceKey> sourceKeys;
	};

	class ShaderLibrary : public resource_management::IResource
	{
	public:
		//castl::unordered_map<ShaderSourceKey, castl::shared_ptr<ShaderResource>> m_ShaderResources;
		castl::unordered_map<cahash::sha256_hash::result_type, ShaderCode> m_ShaderPrograms;
		virtual void Serialize(ca_io::WBatch* inWriter) override {}
		virtual void Deserialize(ca_io::IOBatch* inReader) override {}
	};
}