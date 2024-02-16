#pragma once
#include <ShaderProvider.h>
#include <CASTL/CAString.h>
#include <CASTL/CAUnorderedMap.h>
#include <zpp_bits.h>
#include <unordered_map>

class TestShaderProvider : public ShaderProvider
{
public:
	friend zpp::bits::access;
	using serialize = zpp::bits::members<2>;
	// 通过 ShaderProvider 继承
	virtual uint64_t GetDataLength(castl::string const& codeType) const override;
	virtual void const* GetDataPtr(castl::string const& codeType) const override;
	virtual castl::string GetUniqueName() const override;

	virtual ShaderProvider::ShaderSourceInfo GetDataInfo(castl::string const& codeType) const override;

	void SetUniqueName(std::string const& uniqueName);

	void SetData(std::string const& codeType, std::string const& entryPoint, void const* dataPtr, uint64_t dataLength);

private:
	std::string m_UniqueName;
	std::unordered_map<
		std::string
		, std::pair<std::string, std::vector<char>>
	> m_Data;
};

