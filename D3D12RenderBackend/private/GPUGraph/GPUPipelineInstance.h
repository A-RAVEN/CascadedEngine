#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <GPUGraph.h>
#include <CASTL/CAArrayRef.h>
#include <ResourceManagment/GPUResourceStates.h>
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <CACore/CASharedDic.h>
#include <CodeGenMeta.h>
#include "GPUResourceBindingInstance.h"

namespace graphics_backend
{
	// 这三个类型都是哈希键（`shared_dic<GPUPipelineStateKey, …>`、`HashObj<InputAssemblyStates>`）。
	// 它们**不进接入清单**：解析这个头（它 include 了 `GPUGraph.h`）时工具拒收（exit 6）——
	// 头本身或它 include 的文件里有错误级诊断，其中 `Interface/RenderInterface/header/ShaderBindingBuilder.h`
	// 里那几行老宏每行报 6 条。所以按设计给的第二条路，在本文件末尾手写哈希特化。
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

// 三个键类型的手写哈希特化（为什么手写见类型定义那里的注释）。口径与生成代码一致：按声明顺序逐字段，
// `HashObj<…>` 成员算它包着的对象（新运行时的 `custom_hash_trait<HashObj<…>>` 就是这么做的），
// 容器走新运行时的容器规则（顺序无关）。
namespace cacore::codegen
{
	template<>
	struct custom_hash_trait<graphics_backend::VertexInputBindingData::InputElementDesc>
	{
		static void hash(graphics_backend::VertexInputBindingData::InputElementDesc const& obj, auto& hasher)
		{
			hasher.hash(obj.SemanticName);
			hasher.hash(obj.SemanticIndex);
			hasher.hash(obj.Format);
			hasher.hash(obj.InputSlot);
			hasher.hash(obj.AlignedByteOffset);
			hasher.hash(obj.InputSlotClass);
			hasher.hash(obj.InstanceDataStepRate);
		}
	};

	template<>
	struct custom_hash_trait<graphics_backend::VertexInputBindingData>
	{
		static void hash(graphics_backend::VertexInputBindingData const& obj, auto& hasher)
		{
			hasher.hash(obj.inoutBindingNameToIndex);
			hasher.hash(obj.outVertexAttributes);
		}
	};

	template<>
	struct custom_hash_trait<graphics_backend::GPUPipelineStateKey>
	{
		static void hash(graphics_backend::GPUPipelineStateKey const& obj, auto& hasher)
		{
			hasher.hash(obj.m_ShaderInfo);
			hasher.hash(obj.m_PipelineStates);
			hasher.hash(obj.m_InputAssemblyStates);
			hasher.hash(obj.m_AttachmentFormats);
			hasher.hash(obj.m_DepthFormat);
			hasher.hash(obj.m_VertexInputBindingData);
		}
	};
}