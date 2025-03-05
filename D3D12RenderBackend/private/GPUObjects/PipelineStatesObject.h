#pragma once
#include <Utils/D3D12SubobjectBase.h>
//#include <GPUGraph.h>
#include <CNativeRenderPassInfo.h>
#include <Utils/HashDictionary.h>
#include <GPUObjects/ShaderObject.h>

namespace graphics_backend
{
	struct ShaderStateDescriptor
	{
	public:
		ComPtr<ID3D12RootSignature> rootSignature = nullptr;
		ComPtr<ID3DBlob> vertexShader = nullptr;
		ComPtr<ID3DBlob> fragmentShader = nullptr;
		auto operator<=>(const ShaderStateDescriptor&) const = default;
	};

	struct D3DVertexBindingData
	{
		cacore::NameHash SemanticName;
		UINT SemanticIndex;
		DXGI_FORMAT Format;
		UINT AlignedByteOffset;
		//D3D12_INPUT_CLASSIFICATION InputSlotClass;
		UINT InstanceDataStepRate;
		auto operator<=>(const D3DVertexBindingData&) const = default;
	};

	struct D3DVertexBindingSlot
	{
		UINT InputSlot;
		UINT Stride;
		D3D12_INPUT_CLASSIFICATION InputSlotClass;
		castl::vector<D3DVertexBindingData> VertexBindings;
		auto operator <=> (const D3DVertexBindingSlot&) const = default;
	};


	struct PipelineStateDesc
	{
		D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType;
		cacore::HashObj<CPipelineStateObject> pipelineStates;
		castl::vector<D3DVertexBindingSlot> vertexInputDescs;
		cacore::HashObj <ShaderStateDescriptor> shaderStatesDesc;
		cacore::HashObj <CRenderPassInfo> renderPassInfo;
		uint32_t subpassIndex;
		auto operator <=> (const PipelineStateDesc&) const = default;
	};
	class PipelineStates : public D3D12SubobjectBase
	{
	public:
		PipelineStates(RenderBackend_D3D12* app);
		PipelineStates& operator=(PipelineStates&& other) noexcept = default;
		void Init(PipelineStateDesc const& pipelineStateDesc);
		ComPtr<ID3D12PipelineState> m_PiplineStates;
	};

	using PipelineStatesDic = HashDictionary<PipelineStateDesc, PipelineStates>;
}