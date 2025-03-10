#include "GPUGraphExecutor.h"
#include <ShaderLibrary/D3D12ShaderStruct.h>

namespace graphics_backend
{

	class BasePassDependency
	{
	public:
		PassBase const& thisPass;
		castl::unordered_set<BasePassDependency*> predecessors;
		castl::unordered_set<BasePassDependency*> successors;
		BasePassDependency(PassBase const& pass) : thisPass(pass) {}
		void CheckAddPredecessor(BasePassDependency* predPass)
		{
			bool hasDependency = false;
			//TODO: check Dependency
			if (hasDependency)
			{
				predecessors.insert(predPass);
				predPass->successors.insert(this);
			}
		}
	};

	static void ForeachRenderPassShaderStructs(RenderPass const& renderPass, castl::function<void(D3D2ShaderStruct const&)> callback)
	{
		castl::deque<castl::shared_ptr<ShaderStruct>> shaderStructs;
		for (auto shaderStruct : renderPass.GetShaderStructs())
		{
			CA_ASSERT_BREAK(shaderStruct.second != nullptr, "Shader Struct Is Null, Why!?");
			shaderStructs.push_back(shaderStruct.second);
		}
		auto& drawcallBatchs = renderPass.GetDrawCallBatches();
		for (auto& batch : drawcallBatchs)
		{
			for (auto shaderStruct : batch.shaderStructs)
			{
				CA_ASSERT_BREAK(shaderStruct.second != nullptr, "Shader Struct Is Null, Why!?");
				shaderStructs.push_back(shaderStruct.second);
			}
		}
		while (!shaderStructs.empty())
		{
			auto shaderStruct = shaderStructs.front();
			D3D2ShaderStruct* pStruct = static_cast<D3D2ShaderStruct*>(shaderStruct.get());
			CA_ASSERT_BREAK(pStruct != nullptr, "Shader Struct Is Null, Why!?");
			callback(*pStruct);
			shaderStructs.pop_front();
			for (auto& subArgPairs : pStruct->GetSubStructs())
			{
				for (auto subStruct : subArgPairs.second)
				{
					shaderStructs.push_back(subStruct);
				}
			}
		}
	}


	static void ForeachComputePassShaderStructs(ComputeBatch const& computePass, castl::function<void(D3D2ShaderStruct const&)> callback)
	{
		castl::deque<castl::shared_ptr<D3D2ShaderStruct>> shaderStructs;
		for (auto& shaderStruct : computePass.shaderStructs)
		{
			shaderStructs.push_back(castl::static_pointer_cast<D3D2ShaderStruct>(shaderStruct.second));
		}
		for (auto& dispatch : computePass.dispatchs)
		{
			for (auto& shaderStruct : dispatch.shaderStructs)
			{
				shaderStructs.push_back(castl::static_pointer_cast<D3D2ShaderStruct>(shaderStruct.second));
			}
		}
		while (!shaderStructs.empty())
		{
			auto shaderStruct = shaderStructs.front();
			callback(*shaderStruct);
			shaderStructs.pop_front();
			for (auto& subArgPairs : shaderStruct->GetSubStructs())
			{
				for (auto subStruct : subArgPairs.second)
				{
					shaderStructs.push_back(castl::static_pointer_cast<D3D2ShaderStruct>(subStruct));
				}
			}
		}
	}


	void PassBase::CollectShaderStructResourcesForBasePass(D3D2ShaderStruct const& shaderStruct)
	{
		auto pStructData = shaderStruct.GetStructData();
		auto& imageHandles = shaderStruct.GetImageHandles();
		auto& bufferHandles = shaderStruct.GetBufferHandles();

		for (auto& textureData : pStructData->m_Textures)
		{
			auto found = imageHandles.find(textureData.m_Name);
			if (found != imageHandles.end())
			{
				for (auto img : found->second)
				{
					switch (textureData.m_RWType)
					{
					case ShaderCompilerSlang::EShaderResourceAccess::eReadOnly:
						m_ReadImages.insert(img.first);
						break;
					case ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly:
						m_WriteImages.insert(img.first);
						break;
					case ShaderCompilerSlang::EShaderResourceAccess::eReadWrite:
						m_ReadImages.insert(img.first);
						m_WriteImages.insert(img.first);
						break;
					}
				}
			}
		}

		for (auto& bufferData : pStructData->m_Buffers)
		{
			auto found = bufferHandles.find(bufferData.m_Name);
			if (found != bufferHandles.end())
			{
				for (auto buf : found->second)
				{
					switch (bufferData.m_RWType)
					{
					case ShaderCompilerSlang::EShaderResourceAccess::eReadOnly:
						m_ReadBuffers.insert(buf);
						break;
					case ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly:
						m_WriteBuffers.insert(buf);
						break;
					case ShaderCompilerSlang::EShaderResourceAccess::eReadWrite:
						m_ReadBuffers.insert(buf);
						m_WriteBuffers.insert(buf);
						break;
					}
				}
			}
		}
	}

	void RasterizationPass::Prepare(RenderPass const& renderPass)
	{
		auto& attachments = renderPass.GetAttachments();

		for (auto& attachment : attachments)
		{
			//TODO: Fully Check Read/Write State Of Attachments
			m_ReadImages.insert(attachment);
			m_WriteImages.insert(attachment);
		}

		auto& drawCallBatchs = renderPass.GetDrawCallBatches();
		for (auto& drawCallBatch : drawCallBatchs)
		{
			for (auto& drawCall : drawCallBatch.m_DrawCalls)
			{
				if (drawCall.GetDrawInfo().drawIndexed)
				{
					m_ReadBuffers.insert(drawCall.GetIndexBuffer().indexBufferHandle);
				}
				for (auto& vertexBuffer : drawCall.GetVertexBuffers())
				{
					m_ReadBuffers.insert(vertexBuffer.second);
				}
			}
		}

		ForeachRenderPassShaderStructs(renderPass, [&](D3D2ShaderStruct const& shaderStruct)
		{
			CollectShaderStructResourcesForBasePass(shaderStruct);
		});
	}
	
	void ComputePass::Prepare(ComputeBatch const& computePass)
	{
		ForeachComputePassShaderStructs(computePass, [&](D3D2ShaderStruct const& shaderStruct)
		{
			CollectShaderStructResourcesForBasePass(shaderStruct);
		});
	}

}