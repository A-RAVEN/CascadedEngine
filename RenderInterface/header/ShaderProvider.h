#pragma once
#include <CASTL/CAString.h>
#include <uhash.h>
#include <Compiler.h>

using namespace hash_utils;

struct ShaderSourceInfo
{
	ECompileShaderType compileShaderType;
	uint64_t dataLength;
	void const* dataPtr;
	castl::string entryPoint;
};

class ShaderProvider
{
public:
	virtual uint64_t GetDataLength(castl::string const& codeType) const = 0;
	virtual void const* GetDataPtr(castl::string const& codeType) const = 0;
	virtual castl::string GetUniqueName() const = 0;
	virtual ShaderSourceInfo GetDataInfo(castl::string const& codeType) const = 0;
	bool operator==(ShaderProvider const& other) const
	{
		return GetUniqueName() == other.GetUniqueName();
	}
	template <class HashAlgorithm>
	friend void hash_append(HashAlgorithm& h, ShaderProvider const& provider) noexcept
	{
		hash_append(h, provider.GetUniqueName());
	}
};

struct GraphicsShaderSet
{
	ShaderProvider const* vert = nullptr;
	ShaderProvider const* frag = nullptr;

	bool operator==(GraphicsShaderSet const& other) const
	{
		return (vert == other.vert || (vert != nullptr && other.vert != nullptr && (*vert == *other.vert)))
			&& (frag == other.frag || (frag != nullptr && other.frag != nullptr && (*frag == *other.frag)));
	}

	template <class HashAlgorithm>
	friend void hash_append(HashAlgorithm& h, GraphicsShaderSet const& provider) noexcept
	{
		hash_append(h, provider.vert);
		hash_append(h, provider.frag);
	}
};

struct IShaderSet
{
	virtual EShaderTypeFlags GetShaderTypeFlags(ShaderCompilerSlang::EShaderTargetType shaderTargetType) const = 0;
	virtual ShaderSourceInfo GetShaderSourceInfo(ShaderCompilerSlang::EShaderTargetType shaderTargetType, ECompileShaderType compileShaderType) const = 0;
	virtual ShaderCompilerSlang::ShaderReflectionData const& GetShaderReflectionData(ShaderCompilerSlang::EShaderTargetType shaderTargetType) const = 0;
	virtual castl::string GetUniqueName() const = 0;

	bool operator==(IShaderSet const& other) const
	{
		return GetUniqueName() == other.GetUniqueName();
	}

	template <class HashAlgorithm>
	friend void hash_append(HashAlgorithm& h, IShaderSet const& provider) noexcept
	{
		hash_append(h, provider.GetUniqueName());
	}
};