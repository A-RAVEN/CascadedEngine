#pragma once
#include <HashKeys.h>   // 必须在 CAUnorderedSet.h 之前：castl::unordered_set 的默认参数是 cacore::hash<Key>
#include <CASTL/CAUnorderedSet.h>
#include <CAResource/IResource.h>
#include <Common.h>
#include <CodeGenMeta.h>
#include <Compiler.h>
#include <D3D12Includes.h>

namespace graphics_backend
{
	CA_REFLECT(serializable)
	struct ShaderSourceKey
	{
		cacore::PathHash path;
		cacore::NameHash entryPoint;
		auto operator<=>(const ShaderSourceKey&) const = default;
	};

	CA_REFLECT(serializable)
	struct CBufferBindingInfo
	{
		uint32_t spaceID;
		uint32_t elementCount;
		uint32_t bindingID;
		uint32_t descTableID;
		uint32_t usageMask;
		cacore::NameHash cbufferStructName;
	};

	CA_REFLECT(serializable)
	struct ImageBindingInfo
	{
		ShaderCompilerSlang::EShaderResourceType resourceType;
		ShaderCompilerSlang::EShaderResourceAccess accessType;
		uint32_t spaceID;
		uint32_t elementCount;
		uint32_t bindingID;
		uint32_t descTableID;
		uint32_t usageMask;
		cacore::NameHash imageBindingName;
		bool isUAV() const
		{
			return (resourceType == ShaderCompilerSlang::EShaderResourceType::eRWTexture) ||
				(resourceType == ShaderCompilerSlang::EShaderResourceType::eRWStructuredBuffer);
		}
	};

	CA_REFLECT(serializable)
	struct BufferBindingInfo
	{
		ShaderCompilerSlang::EShaderResourceType resourceType;
		ShaderCompilerSlang::EShaderResourceAccess accessType;
		uint32_t spaceID;
		uint32_t elementCount;
		uint32_t bindingID;
		uint32_t descTableID;
		uint32_t usageMask;
		cacore::NameHash bufferBindingName;
		bool isUAV() const
		{
			return (resourceType == ShaderCompilerSlang::EShaderResourceType::eRWTexture) ||
				(resourceType == ShaderCompilerSlang::EShaderResourceType::eRWStructuredBuffer);
		}
	};

	CA_REFLECT(serializable)
	struct SamplerBindingInfo
	{
		uint32_t spaceID;
		uint32_t elementCount;
		uint32_t bindingID;
		uint32_t descTableID;
		uint32_t usageMask;
		cacore::NameHash samplerBindingName;
	};

	CA_REFLECT(serializable)
	struct StructBindingInfos
	{
		static StructBindingInfos Create(cacore::NameHash const& name, uint32_t elementCount)
		{
			StructBindingInfos result{};
			result.structBindingName = name;
			result.elementCount = elementCount;
			result.subStructOffset = 0;
			result.subStructCount = 0;
			return result;
		}
		void InitSubStructs(uint32_t offset, uint32_t count)
		{
			subStructOffset = offset;
			subStructCount = count;
		}
		uint32_t elementCount;
		uint32_t subStructOffset;
		uint32_t subStructCount;
		cacore::NameHash structBindingName;
		castl::vector<uint32_t> cbufferRefs;
		castl::vector<uint32_t> imageRefs;
		castl::vector<uint32_t> bufferRefs;
		castl::vector<uint32_t> samplerRefs;
	};

	CA_REFLECT(serializable)
	struct ShaderResourceBindingInfo
	{
		uint32_t resourceDescCount;
		uint32_t samplerDescCount;
		int resourceHeapParamID;
		int samplerHeapParamID;
		castl::vector<StructBindingInfos> structBindingInfos;
		castl::vector<CBufferBindingInfo> cbufferInfos;
		castl::vector<ImageBindingInfo> imageInfo;
		castl::vector<BufferBindingInfo> bufferInfos;
		castl::vector<SamplerBindingInfo> samplerInfos;
		ComPtr<ID3DBlob> serializedRootSignatureData;

		void EmplaceStruct(cacore::NameHash const& name, uint32_t count)
		{
			structBindingInfos.push_back(StructBindingInfos::Create(name, count));
		}
	};

	CA_REFLECT(serializable)
	struct ShaderFileInfo
	{
		CA_REFLECT(serializable)
	struct ProgramInfo
		{
			cacore::NameHash entryPointName;
			cahash::sha256_hash::result_type programHash;
			ECompileShaderType shaderType;
		};
		cacore::PathHash path;
		castl::vector<ProgramInfo> entryPointToShaderProgram;
		ShaderCompilerSlang::ShaderReflectionData reflectionData;
		ShaderResourceBindingInfo shaderBindingInfo;
		EShaderTypeFlags GetShaderStageUsage(uint32_t usageMask) const;
		auto operator<=>(const ShaderFileInfo&) const = default;
	};

	CA_REFLECT(serializable)
	struct ShaderCode
	{
		ECompileShaderType shaderType;
		ComPtr<ID3DBlob> data;
		castl::unordered_set<ShaderSourceKey> sourceKeys;
	};

	// 这个类型**故意不加标注**：它有一个非拥有的反向指针成员 `reflectionData`（指向某个
	// `ShaderFileInfo` 里的那份数据），而指针成员在发序列化 / 哈希时会被工具拒绝（退出码 15，
	// stderr 点名 `reflectionData`）—— 老运行时对指针是"直接跳过"（数据静默丢失），新运行时不会跳过。
	// 它本身只是 `GetShaderCodes()` 的临时返回值，不参与落盘，也不出现在 `ShaderLibrary` 的成员里，
	// 所以正确的处置是不给它生成代码，而不是让它带着一个写不出意义的成员进接入清单。
	struct ShaderSetData
	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> fragmentShader;
		ComPtr<ID3DBlob> computeShader;
		ShaderCompilerSlang::ShaderReflectionData const* reflectionData;
	};

	CA_REFLECT(serializable)
class ShaderLibrary : public resource_management::TResource<ShaderLibrary>
	{
	public:
		castl::unordered_map<cacore::PathHash, ShaderFileInfo> m_ShaderFiles;
		castl::unordered_map<cahash::sha256_hash::result_type, ShaderCode> m_ShaderPrograms;
		castl::unordered_map<cacore::NameHash, ShaderCompilerSlang::ShaderStructData> m_ShaderStructs;
		castl::unordered_map<cacore::PathHash, ShaderCompilerSlang::ShaderStructData> m_ShaderRootStructs;
		ShaderFileInfo const* GetShaderFileInfo(cacore::PathHash const& path) const;
		ShaderCode const* GetShaderCode(cahash::sha256_hash::result_type const& shaHash) const;
	};
}

// CodeGen 生成物：上面这些类型各自的 `<类型>.codegen.h`（一个类型一份，反射 / 序列化 / 哈希三节都在
// 里面）。接在这里（类型定义之后）而不是让每个消费方自己记一遍 —— `TResource<T>` 的读写与 `HashObj<T>`
// 的哈希在**实例化**时就需要看到这些类型的全部特化（包括它们成员用到的类型），少一个，那一层就会退回
// "整块字节"或直接编不过。
#include "ShaderLibrary.codegen.h"
#include "ShaderSourceKey.codegen.h"
#include "CBufferBindingInfo.codegen.h"
#include "ImageBindingInfo.codegen.h"
#include "BufferBindingInfo.codegen.h"
#include "SamplerBindingInfo.codegen.h"
#include "StructBindingInfos.codegen.h"
#include "ShaderResourceBindingInfo.codegen.h"
#include "ShaderFileInfo.codegen.h"
#include "ProgramInfo.codegen.h"
#include "ShaderCode.codegen.h"
// cahash::sha256_hash::result_type 是上面那两个映射的键：它的特化也得在这里可见。
#include "result_type.codegen.h"
