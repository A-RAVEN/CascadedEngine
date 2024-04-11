#include <pch.h>
#include "GPUGraphExecutor.h"
#include <VulkanApplication.h>

namespace graphics_backend
{
	void GPUGraphExecutor::PrepareGraph()
	{
	}

	void GPUGraphExecutor::PrepareFrameBufferAndPSOs()
	{
		auto renderPasses = m_Graph.GetRenderPasses();
		for (auto& renderPass : renderPasses)
		{
			auto& drawcallBatchs = renderPass.GetDrawCallBatches();



			for (auto& batch : drawcallBatchs)
			{
				auto& psoDesc = batch.pipelineStateDesc;
				auto vertShader = GetGPUObjectManager().m_ShaderModuleCache.GetOrCreate(psoDesc.m_ShaderSet->GetShaderSourceInfo("spirv", ECompileShaderType::eVert));
				auto fragShader = GetGPUObjectManager().m_ShaderModuleCache.GetOrCreate(psoDesc.m_ShaderSet->GetShaderSourceInfo("spirv", ECompileShaderType::eFrag));

				auto& vertexInputBindings = psoDesc.m_VertexInputBindings;
				auto& vertexAttributes = psoDesc.m_ShaderSet->GetShaderReflectionData().m_VertexAttributes;
				psoDesc.m_InputAssemblyStates;
				CVertexInputDescriptor vertexInputDesc;

				CPipelineObjectDescriptor psoDescObj;
				psoDescObj.vertexInputs = vertexInputDesc;
				psoDescObj.pso = psoDesc.m_PipelineStates;
				psoDescObj.shaderState = { vertShader, fragShader };
			}
		}
	}
}