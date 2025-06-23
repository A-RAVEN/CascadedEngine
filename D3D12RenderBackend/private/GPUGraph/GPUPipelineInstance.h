#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <GPUGraph.h>
#include <CASTL/CAArrayRef.h>
#include <ResourceManagment/GPUResourceStates.h>
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <CACore/CASharedDic.h>
#include "GPUResourceBindingInstance.h"

namespace graphics_backend
{
	struct VertexInputBindingData
	{
		castl::vector<cacore::NameHash> inoutBindingNameToIndex;
		castl::deque<castl::string> sematicNames;
		castl::vector<D3D12_INPUT_ELEMENT_DESC> outVertexAttributes;
		auto operator<=>(VertexInputBindingData const& other) const = default;
	};

	struct GPUPipelineStateKey
	{
		void Init(RenderBackend_D3D12* app
			, PipelineDescData const& pipelineDesc
			, castl::vector<ImageHandle> const& attachments
			, uint32_t depthAttachmentIndex
			, DrawCallBatch const& drawCallBatch
			, D3D12GraphLocalResourceManager const& resourceManager);
		//ComPtr<ID3D12RootSignature> pRootSignature;
		ShaderInfo m_ShaderInfo;
		cacore::HashObj<CPipelineStateObject> m_PipelineStates;
		cacore::HashObj<InputAssemblyStates> m_InputAssemblyStates;
		castl::vector<ETextureFormat> m_AttachmentFormats;
		ETextureFormat m_DepthFormat;
		VertexInputBindingData m_VertexInputBindingData;
		auto operator<=>(GPUPipelineStateKey const& other) const = default;
	};
	static_assert(cacore::hashable<GPUPipelineStateKey>, "not hashable");

	class GPUPipelineInstance
	{
	public:
		void Init(RenderBackend_D3D12* app, GPUPipelineStateKey const& pipelineStateKey);
		ComPtr<ID3D12PipelineState> GetPipelineState() const
		{
			return m_PipelineState;
		}
		int GetResourceHeapParamID() const {
			return m_ResourceHeapParamIndex;
		}
		int GetSamplerHeapParamID() const {
			return m_SamplerHeapParamIndex;
		}
	private:
		ComPtr<ID3D12PipelineState> m_PipelineState;
		int m_ResourceHeapParamIndex;
		int m_SamplerHeapParamIndex;
	};

	class GPUPipelineManager : public D3D12SubobjectBase
	{
	public:
		GPUPipelineManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {}
		GPUPipelineInstance const* GetPipelineState(GPUPipelineStateKey const& stateKey);
	private:
		castl::shared_dic<GPUPipelineStateKey, GPUPipelineInstance> m_SharedDic;
	};

}