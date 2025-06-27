#include "GPUPipelineInstance.h"
#include <Utils/InterfaceTranslation.h>
#include <RenderBackend_D3D12.h>
#include <D3D12Debug.h>

namespace graphics_backend
{

	void MakeVertexInputDescriptorsNew(
		castl::vector<ShaderCompilerSlang::ShaderVertexAttributeData> const& vertexAttributes
		, castl::unordered_map<cacore::NameHash, cacore::HashObj<VertexInputsDescriptor>> const& boundVertexBuffers
		, VertexInputBindingData& outBindingData)
	{
		auto findBoundVertexDescriptorWithSematics = [&](cacore::NameHash const& sematicName
			, uint32_t sematicIndex
			, VertexInputsDescriptor& outVertexInputsDesc
			, VertexAttribute& outAttribute
			, cacore::NameHash& outName)
		{
			for (auto boundPair : boundVertexBuffers)
			{
				auto& attribDesc = boundPair.second;
				for (auto& attribute : attribDesc->attributes)
				{
					if ((attribute.semanticName == sematicName) && (attribute.sematicIndex == sematicIndex))
					{
						outAttribute = attribute;
						outName = boundPair.first;
						outVertexInputsDesc = attribDesc.Get();
						return true;
					}
				}
			}
			return false;
		};
		auto getBindID = [&](cacore::NameHash const& nameHash) -> uint32_t
		{
			for (uint32_t id = 0; id < outBindingData.inoutBindingNameToIndex.size(); ++id)
			{
				if (outBindingData.inoutBindingNameToIndex[id] == nameHash)
				{
					return id;
				}
			}
			outBindingData.inoutBindingNameToIndex.push_back(nameHash);
			return outBindingData.inoutBindingNameToIndex.size() - 1;
		};

		for (auto& attributeData : vertexAttributes)
		{
			bool attribBindingFound = false;
			VertexInputsDescriptor foundDesc{};
			VertexAttribute foundAttribute{};
			cacore::NameHash foundName;
			if (findBoundVertexDescriptorWithSematics(attributeData.m_SematicName
				, attributeData.m_SematicIndex
				, foundDesc, foundAttribute, foundName))
			{
				uint32_t id = getBindID(foundName);

				outBindingData.sematicNames.emplace_back(foundAttribute.semanticName.string());

				D3D12_INPUT_ELEMENT_DESC elementDesc{};
				elementDesc.InputSlot = id;
				elementDesc.Format = VertexInputFormatToDXGIFormat(foundAttribute.format);
				elementDesc.AlignedByteOffset = foundAttribute.offset;
				elementDesc.SemanticIndex = foundAttribute.sematicIndex;
				elementDesc.SemanticName = outBindingData.sematicNames.back().c_str();
				elementDesc.InputSlotClass = foundDesc.perInstance
					? D3D12_INPUT_CLASSIFICATION::D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
					: D3D12_INPUT_CLASSIFICATION::D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
				elementDesc.InstanceDataStepRate = foundDesc.perInstance ? 1 : 0;

				outBindingData.outVertexAttributes.push_back(elementDesc);
			}
			else
			{
				CA_LOG_ERR_BREAK("Vertex Attribute Not Bound For Sematics:{}[{}]", attributeData.m_SematicName, attributeData.m_SematicIndex);
			}
		}
	}

	void GPUPipelineStateKey::Init(RenderBackend_D3D12* app
		, PipelineDescData const& pipelineDesc
		, castl::vector<ImageHandle> const& attachments
		, uint32_t depthAttachmentIndex
		, DrawCallBatch const& drawCallBatch
		, D3D12GraphLocalResourceManager const& resourceManager)
	{
		ShaderFileInfo const* pshaderFileInfo = app->GetShaderFileInfo(pipelineDesc.m_ShaderInfo);
		auto& vertexAttributes = pshaderFileInfo->reflectionData.m_VertexAttributes;
		auto& boundVertexBuffers = drawCallBatch.m_VertexInputDescs;
		MakeVertexInputDescriptorsNew(vertexAttributes, boundVertexBuffers, m_VertexInputBindingData);

		m_ShaderInfo = pipelineDesc.m_ShaderInfo;
		m_PipelineStates = pipelineDesc.m_PipelineStates;
		m_InputAssemblyStates = pipelineDesc.m_InputAssemblyStates;
		m_AttachmentFormats.reserve(attachments.size());
		m_AttachmentFormats.clear();
		m_DepthFormat = ETextureFormat::E_INVALID;
		for (uint32_t id = 0; id < attachments.size(); ++id)
		{
			auto& img = attachments[id];
			auto allocationInfo = resourceManager.GetImageResource(img);
			CA_ASSERT_BREAK(allocationInfo != nullptr, "Image {} Not Created", img.GetName());
			if (id == depthAttachmentIndex)
			{
				CA_ASSERT_BREAK(IsDepthStencilFormat(allocationInfo->resourceDesc.format)
					, "Image {} Should Be Depth Stencil Format", img.GetName());
				m_DepthFormat = allocationInfo->resourceDesc.format;
			}
			else
			{
				m_AttachmentFormats.push_back(allocationInfo->resourceDesc.format);
			}
		}
	}

	void GPUPipelineInstance::Init(RenderBackend_D3D12* app, GPUPipelineStateKey const& pipelineStateKey)
	{
		ShaderFileInfo const* pshaderFileInfo = app->GetShaderFileInfo(pipelineStateKey.m_ShaderInfo);
		m_RootSignature = app->GetRootSignatureManager().GetRootSignature(pshaderFileInfo->shaderBindingInfo.serializedRootSignatureData);
		m_ResourceHeapParamIndex = pshaderFileInfo->shaderBindingInfo.resourceHeapParamID;
		m_SamplerHeapParamIndex = pshaderFileInfo->shaderBindingInfo.samplerHeapParamID;

		ShaderSetData shaderSetData = app->GetShaderCodes(pipelineStateKey.m_ShaderInfo);
		VertexInputBindingData bindingData = pipelineStateKey.m_VertexInputBindingData;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

		// --------------------------
		// Vertex Input Assemblies
		// --------------------------
		psoDesc.pRootSignature = m_RootSignature.Get(); // 已创建的根签名
		psoDesc.VS = { shaderSetData.vertexShader->GetBufferPointer(), shaderSetData.vertexShader->GetBufferSize() };
		psoDesc.PS = { shaderSetData.fragmentShader->GetBufferPointer(), shaderSetData.fragmentShader->GetBufferSize() };
		psoDesc.InputLayout = { bindingData.outVertexAttributes.data(), (uint32_t)bindingData.outVertexAttributes.size() };
		psoDesc.PrimitiveTopologyType = ETopologyToD3D12TopologyType(pipelineStateKey.m_InputAssemblyStates->topology); // 图元类型

		// // --------------------------
		// // RenderTargets
		// // --------------------------
		psoDesc.NumRenderTargets = castl::min<uint32_t>((uint32_t)pipelineStateKey.m_AttachmentFormats.size(), 8);
		for (uint32_t rtID = 0; rtID < pipelineStateKey.m_AttachmentFormats.size(); ++rtID)
		{
			psoDesc.RTVFormats[rtID] = ETextureFormatToDXGIFotmat(pipelineStateKey.m_AttachmentFormats[rtID]); // 匹配交换链格式
		}
		psoDesc.SampleDesc.Count = 1; // 关闭多重采样
		psoDesc.SampleMask = UINT_MAX;

		// --------------------------
		// Rasterization
		// --------------------------
		auto& rasterizeStates = pipelineStateKey.m_PipelineStates->rasterizationStates;
		psoDesc.RasterizerState.FillMode = EPolygonModeToD3D12FillMode(rasterizeStates.polygonMode);
		psoDesc.RasterizerState.CullMode = ECullModeToD3D12CullMode(rasterizeStates.cullMode);
		psoDesc.RasterizerState.FrontCounterClockwise = rasterizeStates.frontFace == EFrontFace::eCounterClockWise
			? TRUE : FALSE; // 顶点顺序是否为逆时针
		psoDesc.RasterizerState.DepthClipEnable = TRUE;

		// --------------------------
		// Blending
		// --------------------------

		D3D12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		auto& colorAttachmentState = pipelineStateKey.m_PipelineStates->colorAttachments;
		for (uint32_t rtID = 0; rtID < psoDesc.NumRenderTargets; ++rtID)
		{
			auto& sourceBlendState = colorAttachmentState.attachmentBlendStates[rtID];
			auto& dstBlendState = blendDesc.RenderTarget[rtID];
			dstBlendState.BlendEnable = sourceBlendState.blendEnable;
			dstBlendState.BlendOp = EBlendOpToD3D12BlendOp(sourceBlendState.colorBlendOp);
			dstBlendState.BlendOpAlpha = EBlendOpToD3D12BlendOp(sourceBlendState.alphaBlendOp);
			dstBlendState.SrcBlend = EBlendFactorToD3D12Blend(sourceBlendState.sourceColorBlendFactor);
			dstBlendState.DestBlend = EBlendFactorToD3D12Blend(sourceBlendState.destColorBlendFactor);
			dstBlendState.SrcBlendAlpha = EBlendFactorToD3D12Blend(sourceBlendState.sourceAlphaBlendFactor);
			dstBlendState.DestBlendAlpha = EBlendFactorToD3D12Blend(sourceBlendState.destAlphaBlendFactor);
		}
		for (uint32_t rtID = psoDesc.NumRenderTargets; rtID < 8; ++rtID)
		{
			blendDesc.RenderTarget[rtID].BlendEnable = FALSE;
		}
		psoDesc.BlendState = blendDesc;

		// --------------------------
		// Depth/Stencil
		// --------------------------
		auto& depthStencilStates = pipelineStateKey.m_PipelineStates->depthStencilStates;
		psoDesc.DepthStencilState.DepthEnable = depthStencilStates.depthTestEnable
			|| depthStencilStates.depthWriteEnable;
		psoDesc.DepthStencilState.DepthFunc = ECompareOpToD3D12Comparison(depthStencilStates.depthCompareOp);
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.StencilEnable = depthStencilStates.stencilTestEnable;
		psoDesc.DepthStencilState.StencilReadMask = depthStencilStates.stencilStateFront.compareMask;
		psoDesc.DepthStencilState.StencilWriteMask = depthStencilStates.stencilStateFront.writeMask;

		auto applyStencilOPState = [&](DepthStencilStates::StencilStates const& srouceState
			, D3D12_DEPTH_STENCILOP_DESC& targetDesc)
		{
			targetDesc.StencilPassOp = EStencilOpToD3D12StencilOp(srouceState.passOp);
			targetDesc.StencilFailOp = EStencilOpToD3D12StencilOp(srouceState.failOp);
			targetDesc.StencilDepthFailOp = EStencilOpToD3D12StencilOp(srouceState.depthFailOp);
			targetDesc.StencilFunc = ECompareOpToD3D12Comparison(srouceState.compareOp);
		};
		applyStencilOPState(depthStencilStates.stencilStateFront, psoDesc.DepthStencilState.FrontFace);
		applyStencilOPState(depthStencilStates.stencilStateBack, psoDesc.DepthStencilState.BackFace);

		if (pipelineStateKey.m_DepthFormat != ETextureFormat::E_INVALID)
		{
			psoDesc.DSVFormat = ETextureFormatToDXGIFotmat(pipelineStateKey.m_DepthFormat);
		}

		ThrowIfFailed(app->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PipelineState)));
	}

	GPUPipelineInstance const* GPUPipelineManager::GetPipelineState(GPUPipelineStateKey const& stateKey)
	{
		auto& inst = m_SharedDic.get_or_create(stateKey, [&](GPUPipelineStateKey const& stateKey) -> GPUPipelineInstance
		{
			GPUPipelineInstance newInstance;
			newInstance.Init(GetApp(), stateKey);
			return newInstance;
		})->second;
		return &inst;
	}

}