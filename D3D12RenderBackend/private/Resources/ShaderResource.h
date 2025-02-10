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

	struct ShaderUniformElementData
	{
	public:
		cacore::NameHash typeName;
		uint32_t offset;
		uint32_t count;
	};

	struct ShaderStructTypeMetaData
	{
	public:
		cacore::NameHash typeName;
		uint32_t size;
		uint32_t stride;
		castl::vector<ShaderUniformElementData> elements;
		auto operator<=>(const ShaderStructTypeMetaData&) const = default;
	};

	class ShaderLibrary : public resource_management::IResource
	{
	public:
		//castl::unordered_map<ShaderSourceKey, castl::shared_ptr<ShaderResource>> m_ShaderResources;
		castl::unordered_map<cahash::sha256_hash::result_type, ShaderCode> m_ShaderPrograms;
		castl::unordered_map<cacore::NameHash, ShaderStructTypeMetaData> m_ShaderStructs;
		virtual void Serialize(ca_io::WBatch* inWriter) override {}
		virtual void Deserialize(ca_io::IOBatch* inReader) override {}
	};
}