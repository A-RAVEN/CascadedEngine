#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <GPUGraph.h>
#include <CASTL/CAArrayRef.h>
#include "GPUResourceStates.h"
#include <ShaderLibrary/D3D12ShaderStruct.h>
/// <summary>
/// Shader Resource Binding Instance With 
/// </summary>
namespace graphics_backend
{



	class GPUResourceBindingInstance : public D3D12SubobjectBase
	{
	public:
		struct GPUResourceSpaceInfo
		{
			uint32_t spaceID;
			castl::vector<D3D2ShaderStruct const*> cbufferStructs;
			castl::vector<BufferHandle> cbufferHandles;
			castl::unordered_map<ImageHandle, ShaderCompilerSlang::EShaderResourceType> imageInfo;
			castl::unordered_map<BufferHandle, ShaderCompilerSlang::EShaderResourceType> bufferInfos;
		};
	public:

		void Init(ShaderInfo const& shaderInfo, castl::array_ref<ShaderStructDic const*> const& shaderStructs);
		void BuildResources(GPUGraph& gpuGraph, D3D12GraphLocalResourceManager& resourceManager);
	private:
		castl::vector<GPUResourceSpaceInfo> m_GPUResourceSpaceInfos;
		ShaderInfo const* p_ShaderInfo;
		ShaderCompilerSlang::ShaderReflectionData const* p_ReflectionData;
	};
}