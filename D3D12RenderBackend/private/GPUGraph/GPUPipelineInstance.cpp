#include "GPUPipelineInstance.h"

namespace graphics_backend
{
	void GPUPipelineStateKey::InitAsRasterizePipeline(ID3D12RootSignature* pRootSignature
		, PipelineDescData const& pipelineDesc
		, castl::vector<ImageHandle> const& attachments
		, DrawCall const& drawCall
		, D3D12GraphLocalResourceManager const& resourceManager)
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

		// --------------------------
		// 基础配置
		// --------------------------
		psoDesc.pRootSignature = pRootSignature; // 已创建的根签名
		psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
		psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
		psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // 图元类型

		// --------------------------
		// 渲染目标格式与混合配置
		// --------------------------
		psoDesc.NumRenderTargets = 0;
		for (ImageHandle const& imageHandle : attachments)
		{
			psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // 匹配交换链格式
		}
		psoDesc.NumRenderTargets = 1;
		psoDesc.SampleDesc.Count = 1; // 关闭多重采样
		psoDesc.SampleMask = UINT_MAX;

		// --------------------------
		// 光栅化状态
		// --------------------------
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.RasterizerState.FrontCounterClockwise = TRUE; // 顶点顺序是否为逆时针
		psoDesc.RasterizerState.DepthClipEnable = TRUE;

		// --------------------------
		// 混合状态
		// --------------------------
		D3D12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		blendDesc.RenderTarget[0].BlendEnable = FALSE; // 默认关闭混合
		psoDesc.BlendState = blendDesc;

		// --------------------------
		// 深度/模板测试
		// --------------------------
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT; // 深度缓冲格式
	}
}