#include "MeshRenderer.h"

MeshGPUData::MeshGPUData(std::shared_ptr<graphics_backend::CRenderBackend> renderBackend)
{
	m_RenderBackend = renderBackend;
}

void MeshGPUData::UploadMeshResource(resource_management::StaticMeshResource* meshResource)
{
	p_MeshResource = meshResource;
	uint32_t vertexCount = meshResource->GetVertexCount();
	uint32_t indexCount = meshResource->GetIndicesCount();
	m_VertexBuffer = m_RenderBackend->CreateGPUBuffer(EBufferUsage::eDataDst | EBufferUsage::eVertexBuffer
		, vertexCount, sizeof(resource_management::CommonVertexData));
	m_IndicesBuffer = m_RenderBackend->CreateGPUBuffer(EBufferUsage::eDataDst | EBufferUsage::eIndexBuffer
		, indexCount, sizeof(uint16_t));

	m_VertexBuffer->ScheduleBufferData(0
		, vertexCount * sizeof(resource_management::CommonVertexData)
		, meshResource->GetVertexData());
	m_VertexBuffer->UploadAsync();

	m_IndicesBuffer->ScheduleBufferData(0
		, indexCount * sizeof(uint16_t)
		, meshResource->GetIndicesData());
	m_IndicesBuffer->UploadAsync();
}

void MeshGPUData::Draw(CInlineCommandList& commandList, uint32_t submeshID, uint32_t instanceCount)
{
	if (m_VertexBuffer->UploadingDone() && m_IndicesBuffer->UploadingDone())
	{
		auto& submeshInfos = p_MeshResource->GetSubmeshInfos();
		commandList.BindVertexBuffers({ m_VertexBuffer.get() });
		commandList.BindIndexBuffers(EIndexBufferType::e16, m_IndicesBuffer.get());
		auto& submeshInfo = submeshInfos[submeshID];
		{
			commandList.DrawIndexed(submeshInfo.m_IndicesCount
			, instanceCount
			, submeshInfo.m_IndexArrayOffset
			, submeshInfo.m_VertexArrayOffset);
		}
	}
}
