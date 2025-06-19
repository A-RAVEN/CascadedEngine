#include "GPUComputePipelineInstance.h"
#include <RenderBackend_D3D12.h>
#include <Utils/InterfaceTranslation.h>
#include <D3D12Debug.h>
namespace graphics_backend
{
	void GPUComputePipelineInstance::Init(RenderBackend_D3D12* app, ShaderInfo const& shaderInfo)
	{
		ShaderFileInfo const* pshaderFileInfo = app->GetShaderFileInfo(shaderInfo);
		ShaderSetData shaderSetData = app->GetShaderCodes(shaderInfo);
		auto pRootSignature = app->GetRootSignatureManager().GetRootSignature(pshaderFileInfo->shaderBindingInfo.serializedRootSignatureData);

		D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
		computePsoDesc.CS = { shaderSetData.computeShader->GetBufferPointer(), shaderSetData.computeShader->GetBufferSize() };
		computePsoDesc.pRootSignature = pRootSignature.Get();

		ThrowIfFailed(app->GetDevice()->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&m_PipelineState)));
	}

}