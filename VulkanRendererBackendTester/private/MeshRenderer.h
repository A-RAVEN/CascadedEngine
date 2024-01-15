#pragma once
#include "StaticMeshResource.h"
#include <RenderInterface/header/CRenderBackend.h>
#include <CACore/header/uhash.h>

using namespace graphics_backend;
class MeshGPUData
{
public:
	MeshGPUData() = default;
	MeshGPUData(std::shared_ptr<graphics_backend::CRenderBackend> renderBackend);
	void UploadMeshResource(resource_management::StaticMeshResource* meshResource);
	void Draw(CInlineCommandList& commandList, uint32_t submeshID, uint32_t instanceCount, uint32_t bindingOffset);
private:
	std::shared_ptr<graphics_backend::CRenderBackend> m_RenderBackend;
	resource_management::StaticMeshResource* p_MeshResource;
	std::shared_ptr<graphics_backend::GPUBuffer> m_VertexBuffer;
	std::shared_ptr<graphics_backend::GPUBuffer> m_IndicesBuffer;
};

struct MeshMaterial
{
	CPipelineStateObject pipelineStateObject;
	GraphicsShaderSet shaderSet;
	std::vector<std::shared_ptr<ShaderBindingSet>> materialShaderBindings;
};

struct MeshRenderer
{
public:
	resource_management::StaticMeshResource* p_MeshResource;
	std::vector<MeshMaterial> materials;
};

struct MeshDrawInfo
{
public:
	resource_management::StaticMeshResource* p_MeshResource;
	uint32_t m_SubmeshIndex;
	std::vector<std::shared_ptr<ShaderBindingSet>> p_ShaderBindings;

	bool operator==(MeshDrawInfo const& rhs) const
	{
		return p_MeshResource == rhs.p_MeshResource
			&& m_SubmeshIndex == rhs.m_SubmeshIndex
			&& p_ShaderBindings == rhs.p_ShaderBindings;
	}

	template <class HashAlgorithm>
	friend void hash_append(HashAlgorithm& h, MeshDrawInfo const& rhs) noexcept
	{
		hash_append(h, rhs.p_MeshResource);
		hash_append(h, rhs.m_SubmeshIndex);
		hash_append(h, rhs.p_ShaderBindings);
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

	static CVertexInputDescriptor GetDescriptor()
	{
		CVertexInputDescriptor descriptor;
		descriptor.AddPrimitiveDescriptor(sizeof(uint32_t), { {0, 0, VertexInputFormat::eR32_UInt} }, true);
		descriptor.AddPrimitiveDescriptor(sizeof(resource_management::CommonVertexData)
			, resource_management::CommonVertexData::GetVertexInputDescs(1), false);
		return descriptor;
	}

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
						cmd.SetShaderBindings(drawCall.first.p_ShaderBindings, 1);
						cmd.BindVertexBuffers({ drawCall.second.first.get() }, {}, 0);
						g_MeshResourceToGPUData[drawCall.first.p_MeshResource].Draw(cmd
						, drawCall.first.m_SubmeshIndex
						, drawCall.second.second.size()
						, 1);
					}
				});
		}
	}

	virtual std::vector<ShaderBindingBuilder> const& GetInterfaceLevelShaderBindingDescriptors() const override
	{
		return m_ShaderBindingDescriptors;
	}

	void AddMeshDrawcall(GraphicsPipelineStatesData const& pipelineStates
		, std::vector<std::shared_ptr<ShaderBindingSet>> shaderBindings
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
			MeshDrawInfo drawInfo{ pMeshResource, submeshID, shaderBindings };
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
		std::vector<GraphicsPipelineStatesData> materialPipelineStates;
		materialPipelineStates.resize(meshRenderer.materials.size());
		for (uint32_t i = 0; i < meshRenderer.materials.size(); ++i)
		{
			materialPipelineStates[i] = GraphicsPipelineStatesData{
			meshRenderer.materials[i].pipelineStateObject
			, GetDescriptor() 
			, meshRenderer.materials[i].shaderSet};
			materialPipelineStates[i].shaderBindingDescriptors.resize(meshRenderer.materials[i].materialShaderBindings.size());
			for (uint32_t mid = 0; mid < meshRenderer.materials[i].materialShaderBindings.size(); ++mid)
			{
				materialPipelineStates[i].shaderBindingDescriptors[mid] = meshRenderer.materials[i].materialShaderBindings[mid]->GetBindingSetDesc();
			}
		}

		auto& instances = meshRenderer.p_MeshResource->GetInstanceInfos();
		auto& submeshs = meshRenderer.p_MeshResource->GetSubmeshInfos();
		for (int instanceID = 0; instanceID < instances.size(); ++instanceID)
		{
			auto& instance = instances[instanceID];
			glm::mat4 instanceTrans = transform * instance.m_InstanceTransform;

			m_Instances.push_back(instanceTrans);
			uint32_t instanceIndex = m_Instances.size() - 1;
			uint32_t matID = submeshs[instance.m_SubmeshID].m_MaterialID;
			AddMeshDrawcall(materialPipelineStates[matID], meshRenderer.materials[matID].materialShaderBindings, instanceIndex, meshRenderer.p_MeshResource, instance.m_SubmeshID);
		}
	}

	void MakeBatch(graphics_backend::CRenderBackend* pBackend)
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
				drawcall.second.first->UploadAsync();
			}
		}
		m_InstanceBuffer = pBackend->CreateGPUBuffer(EBufferUsage::eStructuredBuffer | EBufferUsage::eDataDst
			, m_Instances.size(), sizeof(glm::mat4));
		m_InstanceBuffer->ScheduleBufferData(0
			, m_Instances.size() * sizeof(glm::mat4)
			, m_Instances.data());
		m_InstanceBuffer->UploadAsync();


		ShaderConstantsBuilder shaderConstantDescs{ "PerViewConstants" };
		shaderConstantDescs
			.Mat4<float>("ViewProjectionMatrix");

		ShaderBindingBuilder shaderBindingBuilder{ "TestBinding" };
		shaderBindingBuilder.ConstantBuffer(shaderConstantDescs);
		shaderBindingBuilder.StructuredBuffer("InstanceMatBuffer");

		m_ShaderBindingDescriptors.push_back(shaderBindingBuilder);

		m_PerViewShaderConstants = pBackend->CreateShaderConstantSet(shaderConstantDescs);
		m_PerViewShaderConstants->SetValue("ViewProjectionMatrix", glm::mat4(1.0f));


		m_PerViewShaderBindings = pBackend->CreateShaderBindingSet(shaderBindingBuilder);

		m_PerViewShaderBindings->SetConstantSet(m_PerViewShaderConstants->GetName(), m_PerViewShaderConstants);
		m_PerViewShaderBindings->SetStructBuffer("InstanceMatBuffer", m_InstanceBuffer);
	}

	void Update(glm::mat4 const& viewProjectionMatrix)
	{
		m_PerViewShaderConstants->SetValue("ViewProjectionMatrix", viewProjectionMatrix);
	}
private:
	std::unordered_map<GraphicsPipelineStatesData
		, MeshDrawListInternal
		, hash_utils::default_hashAlg> m_MeshBatchs;
	std::vector<glm::mat4> m_Instances;
	std::vector<ShaderBindingBuilder> m_ShaderBindingDescriptors;
	std::shared_ptr<GPUBuffer> m_InstanceBuffer;

	std::shared_ptr<ShaderConstantSet> m_PerViewShaderConstants;
	std::shared_ptr<ShaderBindingSet> m_PerViewShaderBindings;
};