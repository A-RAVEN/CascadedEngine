#include "pch.h"
#include <ShaderBindingSetHandle.h>
#include "CommandList_Impl.h"
#include "InterfaceTranslator.h"
#include "ShaderBindingSet_Impl.h"

namespace graphics_backend
{
	//CCommandList_Impl::CCommandList_Impl(vk::CommandBuffer cmd
	//	, RenderGraphExecutor* rendergraphExecutor
	//	, castl::shared_ptr<RenderPassObject> renderPassObj
	//	, TIndex subpassIndex
	//	, castl::vector<castl::shared_ptr<CPipelineObject>> const& pipelineObjs
	//) :
	//	m_CommandBuffer(cmd)
	//	, p_RenderGraphExecutor(rendergraphExecutor)
	//	, m_RenderPassObject(renderPassObj)
	//	, m_SubpassIndex(subpassIndex)
	//	, m_PipelineObjects(pipelineObjs)
	//{
	//}
	/*CInlineCommandList& CCommandList_Impl::BindPipelineState(uint32_t pipelineStateId)
	{
		CA_ASSERT(pipelineStateId < m_PipelineObjects.size(), "Invalid Pipeline State Index!");
		m_PipelineObjectIndex = pipelineStateId;
		m_CommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, GetBoundPipelineObject()->GetPipeline());
		return *this;
	}

	CInlineCommandList& CCommandList_Impl::BindVertexBuffers(castl::vector<GPUBuffer const*> pGPUBuffers, castl::vector<uint32_t> offsets, uint32_t firstBinding)
	{
		castl::vector<vk::Buffer> gpuBufferList;
		castl::vector<vk::DeviceSize> offsetList;
		gpuBufferList.resize(pGPUBuffers.size());
		offsetList.resize(pGPUBuffers.size());
		for (uint32_t i = 0; i < pGPUBuffers.size(); ++i)
		{
			gpuBufferList[i] = static_cast<GPUBuffer_Impl const*>(pGPUBuffers[i])
				->GetVulkanBufferObject()->GetBuffer();
		}
		castl::fill(offsetList.begin(), offsetList.end(), 0);
		for (uint32_t i = 0; i < offsets.size(); ++i)
		{
			offsetList[i] = offsets[i];
		}
		m_CommandBuffer.bindVertexBuffers(firstBinding, gpuBufferList, offsetList);
		return *this;
	}

	CInlineCommandList& CCommandList_Impl::BindVertexBuffers(castl::vector<GPUBufferHandle> pGPUBuffers, castl::vector<uint32_t> offsets, uint32_t firstBinding)
	{
		castl::vector<vk::Buffer> gpuBufferList;
		castl::vector<vk::DeviceSize> offsetList;
		gpuBufferList.resize(pGPUBuffers.size());
		offsetList.resize(pGPUBuffers.size());
		for (uint32_t i = 0; i < pGPUBuffers.size(); ++i)
		{
			gpuBufferList[i] = p_RenderGraphExecutor->GetLocalBuffer(pGPUBuffers[i].GetHandleIndex())->GetBuffer();
		}
		castl::fill(offsetList.begin(), offsetList.end(), 0);
		for (uint32_t i = 0; i < offsets.size(); ++i)
		{
			offsetList[i] = offsets[i];
		}
		m_CommandBuffer.bindVertexBuffers(firstBinding, gpuBufferList, offsetList);
		return *this;
	}

	CInlineCommandList& CCommandList_Impl::BindIndexBuffers(EIndexBufferType indexBufferType, GPUBuffer const* pGPUBuffer, uint32_t offset)
	{
		m_CommandBuffer.bindIndexBuffer(static_cast<GPUBuffer_Impl const*>(pGPUBuffer)
			->GetVulkanBufferObject()->GetBuffer(), offset, EIndexBufferTypeTranslate(indexBufferType));
		return *this;
	}

	CInlineCommandList& CCommandList_Impl::BindIndexBuffers(EIndexBufferType indexBufferType, GPUBufferHandle const& gpuBufferHandle, uint32_t offset)
	{
		vk::Buffer vkBuffer = p_RenderGraphExecutor->GetLocalBuffer(gpuBufferHandle.GetHandleIndex())->GetBuffer();
		m_CommandBuffer.bindIndexBuffer(vkBuffer, offset, EIndexBufferTypeTranslate(indexBufferType));
		return *this;
	}

	CInlineCommandList& CCommandList_Impl::SetShaderBindings(castl::vector<castl::shared_ptr<ShaderBindingSet>> bindings, uint32_t firstBinding)
	{
		castl::vector<vk::DescriptorSet> descriptorSets;
		descriptorSets.resize(bindings.size());
		for (uint32_t i = 0; i < bindings.size(); ++i)
		{
			auto binding_set_impl = castl::static_pointer_cast<ShaderBindingSet_Impl>(bindings[i]);
			descriptorSets[i] = binding_set_impl->GetDescriptorSet();
		};
		m_CommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, GetBoundPipelineObject()->GetPipelineLayout()
			, firstBinding, descriptorSets, {});
		return *this;
	}

	CInlineCommandList& CCommandList_Impl::SetShaderBindings(castl::vector<ShaderBindingSetHandle> bindings, uint32_t firstBinding)
	{
		castl::vector<vk::DescriptorSet> descriptorSets;
		descriptorSets.resize(bindings.size());
		for (uint32_t i = 0; i < bindings.size(); ++i)
		{
			auto& descriptorSetObject = p_RenderGraphExecutor->GetLocalDescriptorSet(bindings[i].GetHandleIndex());
			descriptorSets[i] = descriptorSetObject->GetDescriptorSet();
		};
		m_CommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, GetBoundPipelineObject()->GetPipelineLayout()
			, firstBinding, descriptorSets, {});
		return *this;
	}

	CInlineCommandList& CCommandList_Impl::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t indexOffset, uint32_t vertexOffset)
	{
		m_CommandBuffer.drawIndexed(indexCount, instanceCount, indexOffset, vertexOffset, 0);
		return *this;
	}

	CInlineCommandList& CCommandList_Impl::Draw(uint32_t vertexCount, uint32_t instanceCount)
	{
		m_CommandBuffer.draw(vertexCount, instanceCount, 0, 0);
		return *this;
	}

	CInlineCommandList& CCommandList_Impl::SetSissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		m_CommandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(x, y), vk::Extent2D(width, height)));
		return *this;
	}

	castl::shared_ptr<CPipelineObject> CCommandList_Impl::GetBoundPipelineObject() const
	{
		CA_ASSERT(m_PipelineObjectIndex < m_PipelineObjects.size(), "Invalid Pipeline State Index!");
		return m_PipelineObjects[m_PipelineObjectIndex];
	}*/
	CommandList& CommandList_Impl::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t indexOffset, uint32_t vertexOffset, uint32_t firstInstance)
	{
		m_CommandBuffer.drawIndexed(indexCount, instanceCount, indexOffset, vertexOffset, firstInstance);
		return *this;
	}
	CommandList& CommandList_Impl::Draw(uint32_t vertexCount, uint32_t instanceCount)
	{
		m_CommandBuffer.draw(vertexCount, instanceCount, 0, 0);
		return *this;
	}
	CommandList& CommandList_Impl::SetSissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		m_CommandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(x, y), vk::Extent2D(width, height)));
		return *this;
	}
}