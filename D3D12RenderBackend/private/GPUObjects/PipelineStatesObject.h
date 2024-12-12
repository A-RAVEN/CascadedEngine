#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <GPUGraph.h>
#include <CNativeRenderPassInfo.h>

namespace graphics_backend
{
	struct ShaderStateDescriptor
	{
	public:
		ComPtr<ID3D12RootSignature> rootSignature = nullptr;
		//castl::shared_ptr<CShaderModuleObject> vertexShader = nullptr;
		//castl::shared_ptr <CShaderModuleObject> fragmentShader = nullptr;
		auto operator<=>(const ShaderStateDescriptor&) const = default;
	};

	struct PipelineStateDesc
	{
		cacore::HashObj<CPipelineStateObject> pipelineStates;
		cacore::HashObj <CVertexInputDescriptor> vertexInputDesc;
		cacore::HashObj <ShaderStateDescriptor> shaderStatesDesc;
		cacore::HashObj <CRenderPassInfo> renderPassInfo;
		uint32_t subpassIndex;
	};
	class PipelineStates : public D3D12SubobjectBase
	{
	public:
		PipelineStates(RenderBackend_D3D12* app);
		PipelineStates& operator=(PipelineStates&& other) noexcept = default;
		void Init(PipelineStateDesc const& pipelineStateDesc);

	};
}