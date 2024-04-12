#include <pch.h>
#include "GPUGraphExecutor.h"
#include <VulkanApplication.h>
#include <GPUGraphExecutor/ShaderBindingHolder.h>

namespace graphics_backend
{
	struct VertexAttributeBindingData
	{
		uint32_t bindingIndex;
		uint32_t stride;
		castl::vector<VertexAttribute> attributes;
		bool bInstance;
	};

	CVertexInputDescriptor MakeVertexInputDescriptors(castl::map<castl::string, VertexInputsDescriptor> const& vertexInputBindings
		, castl::vector<ShaderCompilerSlang::ShaderVertexAttributeData> const& vertexAttributes
		, InputAssemblyStates assemblyStates
		, castl::unordered_map<castl::string, VertexAttributeBindingData>& inoutBindingNameToIndex)
	{
		CVertexInputDescriptor result;
		result.assemblyStates = assemblyStates;

		result.m_PrimitiveDescriptions.resize(vertexAttributes.size());


		for (auto& attributeData : vertexAttributes)
		{
			for (auto descPair : vertexInputBindings)
			{
				for (auto& attribute : descPair.second.attributes)
				{
					if (attribute.semanticName == attributeData.m_SematicName)
					{
						auto found = inoutBindingNameToIndex.find(descPair.first);
						if (found == inoutBindingNameToIndex.end())
						{
							uint32_t bindingID = inoutBindingNameToIndex.size();
							found = inoutBindingNameToIndex.insert(castl::make_pair(descPair.first, VertexAttributeBindingData{ bindingID , descPair.second.stride, {}, descPair.second.perInstance })).first;
						}
						else
						{
						}
						found->second.attributes.push_back(VertexAttribute{ attributeData.m_Location , attribute.offset, attribute.format });
					}
				}
			}
		}

		return result;
	}

	void GPUGraphExecutor::PrepareGraph(
		)
	{

	}

	void GPUGraphExecutor::PrepareFrameBufferAndPSOs()
	{
		auto renderPasses = m_Graph.GetRenderPasses();
		for (auto& renderPass : renderPasses)
		{
			auto& drawcallBatchs = renderPass.GetDrawCallBatches();
			castl::shared_ptr<RenderPassObject> renderPassObject = nullptr;
			for (auto& batch : drawcallBatchs)
			{
				auto& psoDesc = batch.pipelineStateDesc;
				auto vertShader = GetGPUObjectManager().m_ShaderModuleCache.GetOrCreate(psoDesc.m_ShaderSet->GetShaderSourceInfo("spirv", ECompileShaderType::eVert));
				auto fragShader = GetGPUObjectManager().m_ShaderModuleCache.GetOrCreate(psoDesc.m_ShaderSet->GetShaderSourceInfo("spirv", ECompileShaderType::eFrag));

				//Shader Binding Holder
				ShaderBindingInstance shaderBindingInstance;
				shaderBindingInstance.InitShaderBindings(GetVulkanApplication(), psoDesc.m_ShaderSet->GetShaderReflectionData());

				auto& vertexInputBindings = psoDesc.m_VertexInputBindings;
				auto& vertexAttributes = psoDesc.m_ShaderSet->GetShaderReflectionData().m_VertexAttributes;
				castl::unordered_map<castl::string, VertexAttributeBindingData> inoutBindingNameToIndex;
				CVertexInputDescriptor vertexInputDesc = MakeVertexInputDescriptors(vertexInputBindings, vertexAttributes, psoDesc.m_InputAssemblyStates, inoutBindingNameToIndex);

				CPipelineObjectDescriptor psoDescObj;
				psoDescObj.vertexInputs = vertexInputDesc;
				psoDescObj.pso = psoDesc.m_PipelineStates;
				psoDescObj.shaderState = { vertShader, fragShader };
				psoDescObj.renderPassObject = renderPassObject;
				psoDescObj.descriptorSetLayouts = shaderBindingInstance.m_DescriptorSetsLayouts;
			}
		}
	}
}