#pragma once
#include <CASTL/CAUnorderedSet.h>
#include <CAResource/IResource.h>
#include <Common.h>
#include <Compiler.h>
#include <D3D12Includes.h>

namespace graphics_backend
{
	struct ShaderSourceKey
	{
		cacore::PathHash path;
		cacore::NameHash entryPoint;
		auto operator<=>(const ShaderSourceKey&) const = default;
	};

	struct CBufferBindingInfo
	{
		uint32_t spaceID;
		uint32_t elementCount;
		uint32_t bindingID;
		uint32_t descTableID;
		uint32_t usageMask;
		cacore::NameHash cbufferStructName;
	};

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

	struct SamplerBindingInfo
	{
		uint32_t spaceID;
		uint32_t elementCount;
		uint32_t bindingID;
		uint32_t descTableID;
		uint32_t usageMask;
		cacore::NameHash samplerBindingName;
	};

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

	struct ShaderResourceBindingInfo
	{
		uint32_t resourceDescCount;
		uint32_t samplerDescCount;
		int resourceHeapParamID = -1;
		int samplerHeapParamID = -1;
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

	struct ShaderFileInfo
	{
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

	struct ShaderCode
	{
		ECompileShaderType shaderType;
		ComPtr<ID3DBlob> data;
		castl::unordered_set<ShaderSourceKey> sourceKeys;
	};

	struct ShaderSetData
	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> fragmentShader;
		ComPtr<ID3DBlob> computeShader;
		ShaderCompilerSlang::ShaderReflectionData const* reflectionData;
	};

	class ShaderLibrary : public resource_management::TResource<ShaderLibrary>
	{
	public:
		castl::unordered_map<cacore::PathHash, ShaderFileInfo> m_ShaderFiles;
		castl::unordered_map<cahash::sha256_hash::result_type, ShaderCode> m_ShaderPrograms;
		castl::unordered_map<cacore::NameHash, ShaderCompilerSlang::ShaderStructData> m_ShaderStructs;
		castl::unordered_map<cacore::PathHash, ShaderCompilerSlang::ShaderStructData> m_ShaderRootStructs;
		ShaderFileInfo const* GetShaderFileInfo(cacore::PathHash const& path) const;
		ShaderCode const* GetShaderCode(cahash::sha256_hash::result_type const& shaHash) const;
		friend struct CATypeDescriptor<ShaderLibrary>;
	};
}


CA_REFLECTION(graphics_backend::ShaderLibrary
	, m_ShaderFiles
	, m_ShaderPrograms
	, m_ShaderStructs
	, m_ShaderRootStructs);