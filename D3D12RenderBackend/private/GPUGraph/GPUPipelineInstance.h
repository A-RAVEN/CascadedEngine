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
		//Alias Of D3D12_INPUT_ELEMENT_DESC
		struct InputElementDesc
		{
			cacore::NameHash SemanticName;
			UINT SemanticIndex;
			DXGI_FORMAT Format;
			UINT InputSlot;
			UINT AlignedByteOffset;
			D3D12_INPUT_CLASSIFICATION InputSlotClass;
			UINT InstanceDataStepRate;
			constexpr operator D3D12_INPUT_ELEMENT_DESC() const noexcept
			{
				D3D12_INPUT_ELEMENT_DESC desc;
				desc.SemanticName = SemanticName.c_str();
				desc.SemanticIndex = SemanticIndex;
				desc.Format = Format;
				desc.InputSlot = InputSlot;
				desc.AlignedByteOffset = AlignedByteOffset;
				desc.InputSlotClass = InputSlotClass;
				desc.InstanceDataStepRate = InstanceDataStepRate;
				return desc;
			}
			auto operator<=>(InputElementDesc const& other) const = default;
		};

		castl::vector<cacore::NameHash> inoutBindingNameToIndex;
		castl::vector<InputElementDesc> outVertexAttributes;
		castl::vector<D3D12_INPUT_ELEMENT_DESC> AsInputElementDesc() const
		{
			castl::vector<D3D12_INPUT_ELEMENT_DESC> result;
			result.reserve(outVertexAttributes.size());
			for (auto const& attr : outVertexAttributes)
			{
				result.push_back(attr);
			}
			return result;
		}
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
		ShaderInfo m_ShaderInfo;
		cacore::HashObj<CPipelineStateObject> m_PipelineStates;
		cacore::HashObj<InputAssemblyStates> m_InputAssemblyStates;
		castl::vector<ETextureFormat> m_AttachmentFormats;
		ETextureFormat m_DepthFormat;
		VertexInputBindingData m_VertexInputBindingData;
		bool operator==(const GPUPipelineStateKey& other) const
		{
			bool result = m_ShaderInfo == other.m_ShaderInfo
				&& m_PipelineStates == other.m_PipelineStates
				&& m_InputAssemblyStates == other.m_InputAssemblyStates
				&& m_AttachmentFormats == other.m_AttachmentFormats
				&& m_DepthFormat == other.m_DepthFormat
				&& m_VertexInputBindingData == other.m_VertexInputBindingData;
			if (!result)
			{
				CA_LOG("Not Equal");
				if (m_ShaderInfo != other.m_ShaderInfo)
				{
					CA_LOG("Shader Info Not Equal");
				}
				if (m_PipelineStates != other.m_PipelineStates)
				{
					CA_LOG("Pipeline States Not Equal");
				}
				if (m_InputAssemblyStates != other.m_InputAssemblyStates)
				{
					CA_LOG("Input Assembly States Not Equal");
				}
				if (m_AttachmentFormats != other.m_AttachmentFormats)
				{
					CA_LOG("Attachment Formats Not Equal");
				}
				if (m_DepthFormat != other.m_DepthFormat)
				{
					CA_LOG("Depth Format Not Equal");
				}
				if (m_VertexInputBindingData != other.m_VertexInputBindingData)
				{
					CA_LOG("Vertex Input Binding Data Not Equal");
				}
			}
			return result;
		}
		//auto operator<=>(GPUPipelineStateKey const& other) const = default;
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
		ComPtr<ID3D12RootSignature> const GetRootSignature() const
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

	class GPUPipelineManager : public D3D12SubobjectBase
	{
	public:
		GPUPipelineManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {}
		void Release() override;
		GPUPipelineInstance const* GetPipelineState(GPUPipelineStateKey const& stateKey);
	private:
		castl::shared_dic<GPUPipelineStateKey, GPUPipelineInstance> m_SharedDic;
	};

}