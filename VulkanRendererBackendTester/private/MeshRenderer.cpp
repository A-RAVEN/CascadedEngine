#include "MeshRenderer.h"
#include "StaticMeshResource.h"

using namespace graphics_backend;

VertexInputsDescriptor const g_InstanceDescriptor = VertexInputsDescriptor{
	   sizeof(uint32_t)
	   , true
	   , {VertexAttribute{0, 0, VertexInputFormat::eR32_UInt, "INSTANCEID"}}
};

MeshGPUData::MeshGPUData(castl::shared_ptr<graphics_backend::CRenderBackend> renderBackend)
{
	m_RenderBackend = renderBackend;
	m_VertexInputDescriptor = resource_management::CommonVertexData::GetVertexInputDescriptor();
}

void MeshGPUData::UploadMeshResource(graphics_backend::GPUGraph* gpuGraph, resource_management::StaticMeshResource* meshResource)
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

void MeshGPUData::DrawCall(graphics_backend::DrawCallBatch& drawcallBatch, uint32_t submeshID, uint32_t instanceCount)
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
