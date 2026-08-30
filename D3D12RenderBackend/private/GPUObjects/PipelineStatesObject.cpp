#include "PipelineStatesObject.h"
#include <Utils/InterfaceTranslation.h>
#include <D3D12Debug.h>

namespace graphics_backend
{
	PipelineStates::PipelineStates(RenderBackend_D3D12* app) : D3D12SubobjectBase(app)
	{
	}

	static void PopulateFromPipelineStates(D3D12_GRAPHICS_PIPELINE_STATE_DESC& inoutPipelienStateDesc
		, CRenderPassInfo const& renderPassInfo
		, uint32_t subpassId
		, CPipelineStateObject const& pipelineState)
	{
		auto& subpassInfo = renderPassInfo.subpassInfos[subpassId];
		auto& srcBlendStates = pipelineState.colorAttachments.attachmentBlendStates;
		auto& srcRasterizerState = pipelineState.rasterizationStates;
		auto& dstRasterizerState = inoutPipelienStateDesc.RasterizerState;


		//Rasterizer States
		{
			dstRasterizerState.MultisampleEnable = inoutPipelienStateDesc.SampleDesc.Count > 1;
			dstRasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
			dstRasterizerState.CullMode = ECullModeToD3D12CullMode(srcRasterizerState.cullMode);
			dstRasterizerState.FrontCounterClockwise = EFrontFaceIsClockWise(srcRasterizerState.frontFace);
			dstRasterizerState.DepthBias = 0.0f;
			//dstRasterizerState.DepthBiasClamp = rasterizerSate.depthBiasClamp;
			//dstRasterizerState.SlopeScaledDepthBias = rasterizerSate.slopeScaledDepthBias;
			// unify-depth-clamp-switch: same inverse mapping as GPUPipelineInstance (Vulkan depth-clamp
			// is the opposite of D3D depth-clip-enable) so both D3D pipeline paths behave identically.
			dstRasterizerState.DepthClipEnable = srcRasterizerState.enableDepthClamp ? FALSE : TRUE;
			dstRasterizerState.AntialiasedLineEnable = FALSE;
			dstRasterizerState.ForcedSampleCount = 0;
			dstRasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
		}

		//Multisample State
		{
			inoutPipelienStateDesc.SampleDesc.Count = EMultiSampleCountToUint(pipelineState.msCount);
			inoutPipelienStateDesc.SampleDesc.Quality = 0;
		}
		//Depth Stencil Attachment States
		{
			auto& depthAttachmentInfo = renderPassInfo.GetSubPassDepthAttachmentInfo(subpassId);
			if (pipelineState.depthStencilStates.depthTestEnable)
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
		}
		//Color Attachment States
		{
			inoutPipelienStateDesc.NumRenderTargets = renderPassInfo.GetSubpassColorAttachmentCount(subpassId);
			inoutPipelienStateDesc.BlendState.AlphaToCoverageEnable = FALSE;
			inoutPipelienStateDesc.BlendState.IndependentBlendEnable = TRUE;
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
	}

	void PipelineStates::Init(PipelineStateDesc const& pipelineStateDesc)
	{
		auto& pipelineSates = pipelineStateDesc.pipelineStates.Get();
		auto& shaderStates = pipelineStateDesc.shaderStatesDesc.Get();
		auto& vertexInputDescs = pipelineStateDesc.vertexInputDescs;
		auto& renderPassInfo = pipelineStateDesc.renderPassInfo.Get();

		castl::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

		//Root Signature & Shaders
		{
			psoDesc.pRootSignature = shaderStates.rootSignature.Get();
			psoDesc.VS = CD3DX12_SHADER_BYTECODE(shaderStates.vertexShader.Get());
			psoDesc.PS = CD3DX12_SHADER_BYTECODE(shaderStates.fragmentShader.Get());
		}

		//Vertex Inputs
		{
			psoDesc.PrimitiveTopologyType = pipelineStateDesc.topologyType;
			size_t primitiveDescCount = 0;
			for (uint32_t i = 0; i < vertexInputDescs.size(); ++i)
			{
				primitiveDescCount += vertexInputDescs[i].VertexBindings.size();
			}
			inputElementDescs.resize(primitiveDescCount);
			psoDesc.InputLayout.NumElements = primitiveDescCount;

			{
				size_t inputElementIndex = 0;
				uint32_t inputSlot = 0;
				for (auto& primitiveBindingSlot : vertexInputDescs)
				{
					for (auto& vertexInputBinding : primitiveBindingSlot.VertexBindings)
					{
						auto& destInputElement = inputElementDescs[inputElementIndex];
						destInputElement.SemanticName = vertexInputBinding.SemanticName.c_str();
						destInputElement.SemanticIndex = vertexInputBinding.SemanticIndex;
						destInputElement.Format = vertexInputBinding.Format;
						destInputElement.InputSlot = inputSlot;
						destInputElement.AlignedByteOffset = vertexInputBinding.AlignedByteOffset;
						destInputElement.InputSlotClass = primitiveBindingSlot.InputSlotClass;
						destInputElement.InstanceDataStepRate = primitiveBindingSlot.Stride;
						++inputElementIndex;
					}
					++inputSlot;
				}
			}
			psoDesc.InputLayout.pInputElementDescs = inputElementDescs.data();
		}
		//Others: Color Attachements
		psoDesc.SampleMask = UINT_MAX;
		PopulateFromPipelineStates(psoDesc, renderPassInfo, pipelineStateDesc.subpassIndex, pipelineSates);
		
		ThrowIfFailed(GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PiplineStates)));
	}
}