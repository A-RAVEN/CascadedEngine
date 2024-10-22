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

struct IShaderSet
{
	virtual EShaderTypeFlags GetShaderTypeFlags(ShaderCompilerSlang::EShaderTargetType shaderTargetType) const = 0;
	virtual ShaderSourceInfo GetShaderSourceInfo(ShaderCompilerSlang::EShaderTargetType shaderTargetType
		, ECompileShaderType compileShaderType
		, castl::string_view entryPoint = "") const = 0;
	virtual ShaderCompilerSlang::ShaderReflectionData const& GetShaderReflectionData(ShaderCompilerSlang::EShaderTargetType shaderTargetType) const = 0;
	virtual castl::string GetUniqueName() const = 0;
};