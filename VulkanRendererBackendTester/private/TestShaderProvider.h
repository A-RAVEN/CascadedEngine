#pragma once
#include <RenderInterface/header/ShaderProvider.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <ExternalLib/zpp_bits/zpp_bits.h>

class TestShaderProvider : public ShaderProvider
{
public:
	friend zpp::bits::access;
	using serialize = zpp::bits::members<2>;
	// 通过 ShaderProvider 继承
	virtual uint64_t GetDataLength(std::string const& codeType) const override;
	virtual void const* GetDataPtr(std::string const& codeType) const override;
	virtual std::string GetUniqueName() const override;

	virtual ShaderProvider::ShaderSourceInfo GetDataInfo(std::string const& codeType) const override;

	void SetUniqueName(std::string const& uniqueName);

	void SetData(std::string const& codeType, std::string const& entryPoint, void const* dataPtr, uint64_t dataLength);

private:
	std::string m_UniqueName;
	std::unordered_map<
		std::string
		, std::pair<std::string, std::vector<char>>
	> m_Data;
};

