#include "PipelineStatesObject.h"
#include <Utils/InterfaceTranslation.h>

namespace graphics_backend
{
	PipelineStates::PipelineStates(RenderBackend_D3D12* app) : D3D12SubobjectBase(app)
	{
	}

    void PopulateFromPipelineStates(D3D12_GRAPHICS_PIPELINE_STATE_DESC& inoutPipelienStateDesc, CRenderPassInfo const& renderPassInfo, uint32_t subpassId, CPipelineStateObject const& pipelineState)
    {
        auto& subpassInfo = renderPassInfo.subpassInfos[subpassId];
		inoutPipelienStateDesc.NumRenderTargets = renderPassInfo.GetSubpassColorAttachmentCount(subpassId);
        auto& srcBlendStates = pipelineState.colorAttachments.attachmentBlendStates;
		inoutPipelienStateDesc.BlendState.AlphaToCoverageEnable = FALSE;
		inoutPipelienStateDesc.BlendState.IndependentBlendEnable = TRUE;

		auto& depthAttachmentInfo = renderPassInfo.GetSubPassDepthAttachmentInfo(subpassId);
        if (subpassInfo.depthAttachmentID != INVALID_ATTACHMENT_INDEX)
        {
			
			inoutPipelienStateDesc.DepthStencilState.DepthEnable = TRUE;
			inoutPipelienStateDesc.DepthStencilState.DepthWriteMask = subpassInfo.depthAttachmentReadOnly ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL;
			inoutPipelienStateDesc.DepthStencilState.DepthFunc = ECompareOpToD3D12Comparison(pipelineState.depthStencilStates.depthCompareOp);
		}
		else
		{
			inoutPipelienStateDesc.DepthStencilState.DepthEnable = FALSE;
			inoutPipelienStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			inoutPipelienStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        }
		inoutPipelienStateDesc.DSVFormat = ETextureFormatToDXGIFotmat(renderPassInfo.GetSubPassDepthAttachmentInfo(subpassId).format);
		for (uint32_t i = 0; i < inoutPipelienStateDesc.NumRenderTargets; ++i)
		{
			inoutPipelienStateDesc.RTVFormats[i] = ETextureFormatToDXGIFotmat(renderPassInfo.GetSubPassColorAttachmentInfo(subpassId, i).format);
            
			SingleColorAttachmentBlendStates const& srcBlendState = srcBlendStates[i];
            D3D12_RENDER_TARGET_BLEND_DESC& itrBlendState = inoutPipelienStateDesc.BlendState.RenderTarget[i];
            itrBlendState.BlendEnable = srcBlendState.blendEnable;
			itrBlendState.SrcBlend = EBlendFactorToD3D12Blend(srcBlendState.sourceColorBlendFactor);
			itrBlendState.DestBlend = EBlendFactorToD3D12Blend(srcBlendState.destColorBlendFactor);
			itrBlendState.SrcBlendAlpha = EBlendFactorToD3D12Blend(srcBlendState.sourceAlphaBlendFactor);
			itrBlendState.DestBlendAlpha = EBlendFactorToD3D12Blend(srcBlendState.destAlphaBlendFactor);
			itrBlendState.BlendOp = EBlendOpToD3D12BlendOp(srcBlendState.colorBlendOp);
			itrBlendState.BlendOpAlpha = EBlendOpToD3D12BlendOp(srcBlendState.alphaBlendOp);
            itrBlendState.LogicOpEnable = FALSE;
            itrBlendState.LogicOp = D3D12_LOGIC_OP_CLEAR;
        }
    }

	void PipelineStates::Init(PipelineStateDesc const& pipelineStateDesc)
	{
        auto& pipelineSates = pipelineStateDesc.pipelineStates.Get();
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        //psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1
	}