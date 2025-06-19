#pragma once
#include <Utils/D3D12SubobjectBase.h>
//#include <GPUGraph.h>
//#include <CASTL/CAArrayRef.h>
#include "GPUResourceStates.h"
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <CACore/CASharedDic.h>
#include "GPUResourceBindingInstance.h"

namespace graphics_backend
{
	class GPUComputePipelineInstance
	{
	public:
		void Init(RenderBackend_D3D12* app, ShaderInfo const& shaderInfo);
		ComPtr<ID3D12PipelineState> Get() const
		{
			return m_PipelineState;
		}
	private:
		ComPtr<ID3D12PipelineState> m_PipelineState;
	};

	class GPUComputePipelineManager : public D3D12SubobjectBase
	{
	public:
		GPUComputePipelineManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {}
		GPUComputePipelineInstance const* GetPipelineState(ShaderInfo const& stateKey);
	private:
		castl::shared_dic<ShaderInfo, GPUComputePipelineInstance> m_SharedDic;
	};
}