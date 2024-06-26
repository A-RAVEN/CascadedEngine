#pragma once
#include <Hasher.h>
#include <CRenderBackend.h>
#include "StaticMeshResource.h"
#include <GPUGraph.h>

using namespace graphics_backend;

class MeshGPUData
{
public:
	MeshGPUData() = default;
	MeshGPUData(castl::shared_ptr<graphics_backend::CRenderBackend> renderBackend);
	void UploadMeshResource(graphics_backend::GPUGraph* gpuGraph, resource_management::StaticMeshResource* meshResource);
	void DrawCall(graphics_backend::DrawCallBatch& drawallBatch, uint32_t submeshID, uint32_t instanceCount);
private:
	castl::shared_ptr<graphics_backend::CRenderBackend> m_RenderBackend;
	resource_management::StaticMeshResource* p_MeshResource;
	castl::shared_ptr<graphics_backend::GPUBuffer> m_VertexBuffer;
	castl::shared_ptr<graphics_backend::GPUBuffer> m_IndicesBuffer;
	cacore::HashObj<VertexInputsDescriptor> m_VertexInputDescriptor;
};

static castl::unordered_map<resource_management::StaticMeshResource*, MeshGPUData> g_MeshResourceToGPUData;

static void RegisterMeshResource(castl::shared_ptr<graphics_backend::CRenderBackend> pBackend
	, graphics_backend::GPUGraph* gpuGraph
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
	castl::shared_ptr<graphics_backend::ShaderArgList> shaderArgs;
};

struct MeshRenderer
{
public:
	resource_management::StaticMeshResource* p_MeshResource;
	castl::vector<cacore::HashObj<MeshMaterial>> materials;
};

extern VertexInputsDescriptor const g_InstanceDescriptor;

class MeshBatcher
{
	castl::shared_ptr<graphics_backend::CRenderBackend> pRenderBackend;

	castl::vector<glm::mat4> m_Instances;



	struct SubmeshDrawcallInfo
	{
		MeshGPUData* p_GPUMeshData;
		cacore::HashObj<MeshMaterial> material;
		uint32_t submeshID;
		auto operator<=>(const SubmeshDrawcallInfo&) const = default;
	};

	struct SubmeshDrawcallData
	{
		castl::vector<uint32_t> m_InstanceIDs;
	};

	castl::unordered_map< SubmeshDrawcallInfo, SubmeshDrawcallData> m_DrawCallInfoToDrawCallData;

public:
	MeshBatcher(castl::shared_ptr<graphics_backend::CRenderBackend> pRenderBackend)
		: pRenderBackend(pRenderBackend)
	{
	}

	void AddMeshRenderer(MeshRenderer const& meshRenderer, glm::mat4 const& transform)
	{
		MeshGPUData* pGpuMeshData = &g_MeshResourceToGPUData[meshRenderer.p_MeshResource];

		auto& submeshInfos = meshRenderer.p_MeshResource->GetSubmeshInfos();
		auto& instances = meshRenderer.p_MeshResource->GetInstanceInfos();
		for (int instanceID = 0; instanceID < instances.size(); ++instanceID)
		{
			auto& instance = instances[instanceID];
			glm::mat4 instanceTrans = transform * instance.m_InstanceTransform;
			m_Instances.push_back(glm::transpose(instanceTrans));

			uint32_t instanceIndex = m_Instances.size() - 1;
			auto& submeshInfo = submeshInfos[instance.m_SubmeshID];
			auto& material = meshRenderer.materials[castl::min((size_t)submeshInfo.m_MaterialID, meshRenderer.materials.size() - 1)];

			SubmeshDrawcallInfo drawcallinfo{};
			drawcallinfo.material = material;
			drawcallinfo.p_GPUMeshData = pGpuMeshData;
			drawcallinfo.submeshID = instance.m_SubmeshID;

			auto found = m_DrawCallInfoToDrawCallData.find(drawcallinfo);
			if (found == m_DrawCallInfoToDrawCallData.end())
			{
				found = m_DrawCallInfoToDrawCallData.insert(castl::make_pair(drawcallinfo, SubmeshDrawcallData{})).first;
			}
			found->second.m_InstanceIDs.push_back(instanceIndex);
		}
	}

	void Draw(graphics_backend::GPUGraph* pGraph, graphics_backend::RenderPass* pRenderPass)
	{
		graphics_backend::BufferHandle instanceTransformBuffer{ "InstanceTransformsBuffer" , 0 };
		pGraph->AllocBuffer(instanceTransformBuffer, GPUBufferDescriptor::Create(EBufferUsage::eStructuredBuffer | EBufferUsage::eDataDst, m_Instances.size(), sizeof(glm::mat4)))
			.ScheduleData(instanceTransformBuffer, m_Instances.data(), m_Instances.size() * sizeof(glm::mat4));
		castl::shared_ptr<graphics_backend::ShaderArgList> instanceShaderArgs = castl::make_shared<graphics_backend::ShaderArgList>();
		instanceShaderArgs->SetBuffer("instanceTransforms", instanceTransformBuffer);
		pRenderPass->PushShaderArguments("meshInstanceTransforms", instanceShaderArgs);
		uint32_t index = 0;
		for (auto& pair : m_DrawCallInfoToDrawCallData)
		{
			auto& drawcallInfo = pair.first;
			auto& drawcallInstances = pair.second;
			graphics_backend::BufferHandle instanceIDBuffer{ "MeshInstanceIDBuffer", index++};
			size_t bufferSize = drawcallInstances.m_InstanceIDs.size() * sizeof(uint32_t);
			pGraph->AllocBuffer(instanceIDBuffer, GPUBufferDescriptor::Create(EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, drawcallInstances.m_InstanceIDs.size(), sizeof(uint32_t)))
				.ScheduleData(instanceIDBuffer, drawcallInstances.m_InstanceIDs.data(), bufferSize);

			DrawCallBatch newDrawcallBatch = DrawCallBatch::New();
			newDrawcallBatch.PushArgList("meshMaterialData", drawcallInfo.material->shaderArgs)
				.SetShaderSet(drawcallInfo.material->shaderSet)
				.SetPipelineState(drawcallInfo.material->pipelineStateObject)
				.SetVertexBuffer(g_InstanceDescriptor, instanceIDBuffer);
			drawcallInfo.p_GPUMeshData->DrawCall(newDrawcallBatch, drawcallInfo.submeshID, drawcallInstances.m_InstanceIDs.size());

			pRenderPass->DrawCall(newDrawcallBatch);
		}
	}
};