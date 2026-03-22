#pragma once
#include <Utils/D3D12SubobjectBase.h>
//#include <GPUGraph.h>
//#include <CASTL/CAArrayRef.h>
#include <ResourceManagment/GPUResourceStates.h>
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
		ComPtr<ID3D12RootSignature> const& GetRootSignature() const
		{
			return m_RootSignature;
		}
		int GetResourceHeapParamID() const {
			return m_ResourceHeapParamIndex;
		}
		int GetSamplerHeapParamID() const {
			return m_SamplerHeapParamIndex;
		}
	private:
		ComPtr<ID3D12PipelineState> m_PipelineState;
		ComPtr<ID3D12RootSignature> m_RootSignature;
		int m_ResourceHeapParamIndex;
		int m_SamplerHeapParamIndex;
	};

	class GPUComputePipelineManager : public D3D12SubobjectBase
	{
	public:
		GPUComputePipelineManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {}
		void Release() override;
		GPUComputePipelineInstance const* GetPipelineState(ShaderInfo const& stateKey);
	private:
		castl::shared_dic<ShaderInfo, GPUComputePipelineInstance> m_SharedDic;
	};
}