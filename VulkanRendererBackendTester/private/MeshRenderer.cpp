#include "MeshRenderer.h"
#include "StaticMeshResource.h"

using namespace graphics_backend;
castl::unordered_map<resource_management::StaticMeshResource*, MeshGPUData> g_MeshResourceToGPUData;

MeshGPUData::MeshGPUData(castl::shared_ptr<graphics_backend::CRenderBackend> renderBackend)
{
	m_RenderBackend = renderBackend;
	m_VertexInputDescriptor = resource_management::CommonVertexData::GetVertexInputDescriptor();
}

void MeshGPUData::UploadMeshResource(GPUGraph* gpuGraph, resource_management::StaticMeshResource* meshResource)
{
	p_MeshResource = meshResource;
	uint32_t vertexCount = meshResource->GetVertexCount();
	uint32_t indexCount = meshResource->GetIndicesCount();
	m_VertexBuffer = m_RenderBackend->CreateGPUBuffer(EBufferUsage::eDataDst | EBufferUsage::eVertexBuffer
		, vertexCount, sizeof(resource_management::CommonVertexData));
	m_IndicesBuffer = m_RenderBackend->CreateGPUBuffer(EBufferUsage::eDataDst | EBufferUsage::eIndexBuffer
		, indexCount, sizeof(uint16_t));

	gpuGraph->ScheduleData(m_VertexBuffer, meshResource->GetVertexData(), vertexCount * sizeof(resource_management::CommonVertexData), 0);
	gpuGraph->ScheduleData(m_IndicesBuffer, meshResource->GetIndicesData(), indexCount * sizeof(uint16_t), 0);
}

void MeshGPUData::DrawCall(DrawCallBatch& drawcallBatch, uint32_t submeshID, uint32_t instanceCount, uint32_t bindingOffset)
{
	drawcallBatch.SetIndexBuffer(EIndexBufferType::e16, m_IndicesBuffer);
	drawcallBatch.SetVertexBuffer(m_VertexInputDescriptor, m_VertexBuffer);
	drawcallBatch.Draw([this, submeshID, instanceCount](graphics_backend::CommandList& commandList)
		{
			auto& submeshInfos = p_MeshResource->GetSubmeshInfos();
			auto& submeshInfo = submeshInfos[submeshID];
			commandList.DrawIndexed(submeshInfo.m_IndicesCount
				, instanceCount
				, submeshInfo.m_IndexArrayOffset
				, submeshInfo.m_VertexArrayOffset);
		});
}
