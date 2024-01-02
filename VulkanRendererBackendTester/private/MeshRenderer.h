#pragma once
#include "StaticMeshResource.h"
#include <RenderInterface/header/CRenderBackend.h>
#include <SharedTools/header/uhash.h>

using namespace graphics_backend;
class MeshGPUData
{
public:
	MeshGPUData(std::shared_ptr<graphics_backend::CRenderBackend> renderBackend);
	void UploadMeshResource(resource_management::StaticMeshResource* meshResource);
	void Draw(CInlineCommandList& commandList, uint32_t submeshID, uint32_t instanceCount);
private:
	std::shared_ptr<graphics_backend::CRenderBackend> m_RenderBackend;
	resource_management::StaticMeshResource* p_MeshResource;
	std::shared_ptr<graphics_backend::GPUBuffer> m_VertexBuffer;
	std::shared_ptr<graphics_backend::GPUBuffer> m_IndicesBuffer;
};

struct MeshRenderer
{
public:
	resource_management::StaticMeshResource* p_MeshResource;
	GraphicsPipelineStatesData m_GraphicsPipelineStatesData;
};

struct MeshDrawInfo
{
public:
	resource_management::StaticMeshResource* p_MeshResource;
	uint32_t m_SubmeshIndex;

	bool operator==(MeshDrawInfo const& rhs) const
	{
		return p_MeshResource == rhs.p_MeshResource
			&& m_SubmeshIndex == rhs.m_SubmeshIndex;
	}

	template <class HashAlgorithm>
	friend void hash_append(HashAlgorithm& h, MeshDrawInfo const& rhs) noexcept
	{
		hash_append(h, rhs.p_MeshResource);
		hash_append(h, rhs.m_SubmeshIndex);
	}
};

extern std::unordered_map<resource_management::StaticMeshResource*, MeshGPUData> g_MeshResourceToGPUData;

class MeshBatchDrawInterface : public IDrawBatchInterface
{
public:
	struct MeshDrawListInternal
	{
		TIndex psoID;
		std::unordered_map<MeshDrawInfo, std::pair<std::shared_ptr<GPUBuffer>, std::vector<uint32_t>>, hash_utils::default_hashAlg> drawCalls;
	};
	virtual void OnRegisterGraphicsPipelineStates(IBatchManager& batchManager) override
	{
		for (auto& pipelineState : m_MeshBatchs)
		{
			pipelineState.second.psoID = batchManager.RegisterGraphicsPipelineState(pipelineState.first);
			batchManager.AddBatch([this, refData = &pipelineState.second](CInlineCommandList& cmd)
				{
					cmd.BindPipelineState(refData->psoID);
					cmd.SetShaderBindings({ m_PerViewShaderBindings });
					for (auto drawCall : refData->drawCalls)
					{
						cmd.BindVertexBuffers({ drawCall.second.first.get() }, {}, 0);
						g_MeshResourceToGPUData[drawCall.first.p_MeshResource].Draw(cmd
						, drawCall.first.m_SubmeshIndex
						, drawCall.second.second.size());
					}
				});
		}
	}

	void AddMeshDrawcall(GraphicsPipelineStatesData const& pipelineStates
		, uint32_t instanceIndex
		, resource_management::StaticMeshResource* pMeshResource
		, uint32_t submeshID)
	{
		auto find = m_MeshBatchs.find(pipelineStates);
		if (find == m_MeshBatchs.end())
		{
			find = m_MeshBatchs.insert({ pipelineStates, {} }).first;
		}
		auto& drawListInternal = find->second;
		auto& submeshes = pMeshResource->GetSubmeshInfos();
		{
			MeshDrawInfo drawInfo{ pMeshResource, submeshID };
			auto findDrawCall = drawListInternal.drawCalls.find(drawInfo);
			if (findDrawCall == drawListInternal.drawCalls.end())
			{
				findDrawCall = drawListInternal.drawCalls.insert(std::make_pair(drawInfo, std::make_pair(nullptr, std::vector<uint32_t>{}))).first;
			}
			findDrawCall->second.second.push_back(instanceIndex);
		}
	}

	void AddMesh(MeshRenderer const& meshRenderer, glm::mat4 const& transform)
	{
		m_Instances.push_back(transform);
		uint32_t instanceIndex = m_Instances.size() - 1;

		auto& submeshes = meshRenderer.p_MeshResource->GetSubmeshInfos();
		for (int i = 0; i < submeshes.size(); ++i)
		{
			AddMeshDrawcall(meshRenderer.m_GraphicsPipelineStatesData, instanceIndex, meshRenderer.p_MeshResource, i);
		}
	}

	void MakeMesh(graphics_backend::CRenderBackend* pBackend)
	{
		for (auto& meshBatch : m_MeshBatchs)
		{
			for (auto& drawcall : meshBatch.second.drawCalls)
			{
				drawcall.second.first = pBackend->CreateGPUBuffer(EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst
					, drawcall.second.second.size(), sizeof(uint32_t));
				drawcall.second.first->ScheduleBufferData(0
					, drawcall.second.second.size() * sizeof(uint32_t)
					, drawcall.second.second.data());
			}
		}
	}
private:
	std::unordered_map<GraphicsPipelineStatesData
		, MeshDrawListInternal
		, hash_utils::default_hashAlg> m_MeshBatchs;
	std::vector<glm::mat4> m_Instances;

	std::shared_ptr<ShaderConstantSet> m_PerViewShaderConstants;
	std::shared_ptr<ShaderBindingSet> m_PerViewShaderBindings;
};