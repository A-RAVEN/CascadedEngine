#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <GPUGraph.h>
#include <CASTL/CAArrayRef.h>
#include "GPUResourceStates.h"
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <CACore/CASharedDic.h>
#include "GPUResourceBindingInstance.h"

namespace graphics_backend
{
	struct GPUPipelineStateKey
	{
		void InitAsRasterizePipeline(ID3D12RootSignature* pRootSignature, PipelineDescData const& pipelineDesc
			, castl::vector<ImageHandle> const& attachments
			, DrawCall const& drawCall
			, D3D12GraphLocalResourceManager const& resourceManager);
		ShaderInfo shaderInfo;
	};

	class GPUPipelineInstance : public D3D12SubobjectBase
	{
	public:
		void Init(GPUPipelineStateKey const& pipelineStateKey);
	};
}