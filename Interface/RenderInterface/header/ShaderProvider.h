#pragma once
#include <CASTL/CAString.h>
#include <HashKeys.h>
#include <CodeGenMeta.h>
#include <Compiler.h>

struct ShaderSourceInfo
{
	ECompileShaderType compileShaderType;
	uint64_t dataLength;
	void const* dataPtr;
	cacore::NameHash entryPoint;
	auto operator<=>(const ShaderSourceInfo&) const = default;
};

// 也是哈希键（`shared_dic<ShaderInfo, GPUComputePipelineInstance>`）：换驱动之后必须自己有生成特化。
CA_REFLECT(serializable)
struct ShaderInfo
{
	cacore::PathHash path;
	auto operator<=>(ShaderInfo const& other) const = default;
	constexpr bool isValid() const noexcept
	{
		return path.Valid();
	}
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

// `ShaderInfo` 的生成物（哈希特化是给 `cacore::hash<ShaderInfo>` / `HashObj<ShaderInfo>` 用的）。
#include "ShaderInfo.Reflection.h"
#include "ShaderInfo.Serialization.h"
#include "ShaderInfo.Hash.h"