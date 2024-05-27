#pragma once
#include <CASTL/CAString.h>
#include <Hasher.h>
#include <Compiler.h>

struct ShaderSourceInfo
{
	ECompileShaderType compileShaderType;
	uint64_t dataLength;
	void const* dataPtr;
	castl::string entryPoint;
	auto operator<=>(const ShaderSourceInfo&) const = default;
};

class ShaderProvider
{
public:
	virtual uint64_t GetDataLength(castl::string const& codeType) const = 0;
	virtual void const* GetDataPtr(castl::string const& codeType) const = 0;
	virtual castl::string GetUniqueName() const = 0;
	virtual ShaderSourceInfo GetDataInfo(castl::string const& codeType) const = 0;
};

struct GraphicsShaderSet
{
	ShaderProvider const* vert = nullptr;
	ShaderProvider const* frag = nullptr;
};

struct IShaderSet
{
	virtual EShaderTypeFlags GetShaderTypeFlags(ShaderCompilerSlang::EShaderTargetType shaderTargetType) const = 0;
	virtual ShaderSourceInfo GetShaderSourceInfo(ShaderCompilerSlang::EShaderTargetType shaderTargetType, ECompileShaderType compileShaderType) const = 0;
	virtual ShaderCompilerSlang::ShaderReflectionData const& GetShaderReflectionData(ShaderCompilerSlang::EShaderTargetType shaderTargetType) const = 0;
	virtual castl::string GetUniqueName() const = 0;
};