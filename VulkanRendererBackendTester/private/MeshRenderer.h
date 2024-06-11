#pragma once
#include <uhash.h>
#include <Hasher.h>
#include <CRenderBackend.h>
#include "StaticMeshResource.h"
#include <GPUGraph.h>

class MeshGPUData
{
public:
	MeshGPUData() = default;
	MeshGPUData(castl::shared_ptr<graphics_backend::CRenderBackend> renderBackend);
	void UploadMeshResource(GPUGraph* gpuGraph, resource_management::StaticMeshResource* meshResource);
	void DrawCall(DrawCallBatch& drawallBatch, uint32_t submeshID, uint32_t instanceCount, uint32_t bindingOffset);
private:
	castl::shared_ptr<graphics_backend::CRenderBackend> m_RenderBackend;
	resource_management::StaticMeshResource* p_MeshResource;
	castl::shared_ptr<graphics_backend::GPUBuffer> m_VertexBuffer;
	castl::shared_ptr<graphics_backend::GPUBuffer> m_IndicesBuffer;
	cacore::HashObj<VertexInputsDescriptor> m_VertexInputDescriptor;
};

static castl::unordered_map<resource_management::StaticMeshResource*, MeshGPUData> g_MeshResourceToGPUData;

static void RegisterMeshResource(castl::shared_ptr<graphics_backend::CRenderBackend> pBackend
	, GPUGraph* gpuGraph
	, resource_management::StaticMeshResource* meshResource)
{
	auto found = g_MeshResourceToGPUData.find(meshResource);
	if (found == g_MeshResourceToGPUData.end())
	{
		MeshGPUData gpuData{ pBackend };
		gpuData.UploadMeshResource(gpuGraph, meshResource);
		g_MeshResourceToGPUData.insert(castl::make_pair(meshResource, gpuData));
	}
}

struct MeshMaterial
{
	//基础管线状态
	CPipelineStateObject pipelineStateObject;
	//Shader
	IShaderSet* shaderSet;
	//Shader参数
	castl::shared_ptr<ShaderArgList> shaderArgs;
};

struct MeshRenderer
{
public:
	resource_management::StaticMeshResource* p_MeshResource;
	castl::vector<cacore::HashObj<MeshMaterial>> materials;
};

class MeshBatcher
{
	castl::shared_ptr<CRenderBackend> pRenderBackend;

	castl::vector<glm::mat4> m_Instances;

	struct SubmeshDrawcallInfo
	{
		MeshGPUData* p_GPUMeshData;
		cacore::HashObj<MeshMaterial> material;
		int m_IndicesCount;
		int m_IndexArrayOffset;
		int m_VertexArrayOffset;
	};

	struct SubmeshDrawcallData
	{
		castl::vector<uint32_t> m_InstanceIDs;
	};

	castl::unordered_map< SubmeshDrawcallInfo, SubmeshDrawcallData> m_DrawCallInfoToDrawCallData;

	void AddMeshRenderer(MeshRenderer const& meshRenderer, glm::mat4 const& transform)
	{
		MeshGPUData* pGpuMeshData = &g_MeshResourceToGPUData[meshRenderer.p_MeshResource];

		auto& submeshInfos = meshRenderer.p_MeshResource->GetSubmeshInfos();
		auto& instances = meshRenderer.p_MeshResource->GetInstanceInfos();
		for (int instanceID = 0; instanceID < instances.size(); ++instanceID)
		{
			auto& instance = instances[instanceID];
			glm::mat4 instanceTrans = transform * instance.m_InstanceTransform;
			m_Instances.push_back(instanceTrans);

			uint32_t instanceIndex = m_Instances.size() - 1;
			auto& submeshInfo = submeshInfos[instance.m_SubmeshID];
			auto& material = meshRenderer.materials[castl::min((size_t)submeshInfo.m_MaterialID, meshRenderer.materials.size() - 1)];

			SubmeshDrawcallInfo drawcallinfo{};
			drawcallinfo.material = material;
			drawcallinfo.p_GPUMeshData = pGpuMeshData;
			drawcallinfo.m_IndicesCount = submeshInfo.m_IndicesCount;
			drawcallinfo.m_IndexArrayOffset = submeshInfo.m_IndexArrayOffset;
			drawcallinfo.m_VertexArrayOffset = submeshInfo.m_VertexArrayOffset;

			auto found = m_DrawCallInfoToDrawCallData.find(drawcallinfo);
			if (found == m_DrawCallInfoToDrawCallData.end())
			{
				found = m_DrawCallInfoToDrawCallData.insert(castl::make_pair(drawcallinfo, SubmeshDrawcallData{})).first;
			}
			found->second.m_InstanceIDs.push_back(instanceIndex);
		}
	}

	void Draw(graphics_backend::GPUGraph* pGraph, castl::vector<DrawCallBatch>& drawcallBatchs)
	{
		uint32_t index = 0;
		for (auto pair : m_DrawCallInfoToDrawCallData)
		{
			BufferHandle buffer{ castl::to_string(index++) };
			pGraph->AllocBuffer(buffer, GPUBufferDescriptor::Create(EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, pair.second.m_InstanceIDs.size(), sizeof(uint32_t)))
				.ScheduleData(buffer, pair.second.m_InstanceIDs.data(), pair.second.m_InstanceIDs.size() * sizeof(uint32_t));

			DrawCallBatch newDrawcallBatch = DrawCallBatch::New();
		}
	}
};