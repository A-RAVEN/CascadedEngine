#pragma once
#include "Common.h"
#include "CCommandList.h"
#include "ShaderArgList.h"
#include "CPipelineStateObject.h"
#include "CVertexInputDescriptor.h"
#include "ShaderProvider.h"
#include "ShaderResourceHandle.h"
#include <DebugUtils.h>
#include <CASTL/CAFunctional.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAMap.h>

namespace graphics_backend
{
#pragma region Upload Data Holder
	struct UploadDataHolder
	{
		castl::vector<uint8_t> m_Data;
		uint64_t AddData(void const* pData, uint64_t byteSize)
		{
			uint64_t startPos = m_Data.size();
			if (byteSize > 0)
			{
				m_Data.resize(startPos + byteSize);
				memcpy(&m_Data[startPos], pData, byteSize);
			}
			return startPos;
		}
		void const* GetPtr(uint64_t index) const
		{
			return &m_Data[index];
		}
		void Clear()
		{
			m_Data.clear();
		}
	};
#pragma endregion

#pragma region Render Pass And DrawCalls
	struct PipelineDescData
	{
		IShaderSet const* m_ShaderSet;
		cacore::HashObj<CPipelineStateObject> m_PipelineStates;
		cacore::HashObj <InputAssemblyStates> m_InputAssemblyStates;
		castl::vector<castl::pair<castl::string, castl::shared_ptr<ShaderArgList>>> shaderArgLists;
		//TODO: Do Not Expose This
		static PipelineDescData CombindDescData(PipelineDescData const& parent, PipelineDescData const& child)
		{
			PipelineDescData newDescData{};
			newDescData.m_ShaderSet = child.m_ShaderSet ? child.m_ShaderSet : parent.m_ShaderSet;
			newDescData.m_PipelineStates = child.m_PipelineStates.Valid() ? child.m_PipelineStates : parent.m_PipelineStates;
			newDescData.m_InputAssemblyStates = child.m_InputAssemblyStates.Valid() ? child.m_InputAssemblyStates : parent.m_InputAssemblyStates;
			newDescData.shaderArgLists.reserve(child.shaderArgLists.size() + parent.shaderArgLists.size());
			for (auto argList : parent.shaderArgLists)
			{
				newDescData.shaderArgLists.push_back(argList);
			}
			for (auto argList : child.shaderArgLists)
			{
				newDescData.shaderArgLists.push_back(argList);
			}
			return newDescData;
		}
	};

	class DrawCallBatch
	{
	public:

		static DrawCallBatch New()
		{
			return {};
		}

		//PSO
		PipelineDescData pipelineStateDesc;
		//Draw Calls
		castl::vector<castl::function<void(CommandList&)>> m_DrawCommands;
		castl::unordered_map<cacore::HashObj<VertexInputsDescriptor>, BufferHandle> m_BoundVertexBuffers;
		BufferHandle m_BoundIndexBuffer;
		EIndexBufferType m_IndexBufferType = EIndexBufferType::e16;
		uint32_t m_IndexBufferOffset = 0;

		inline DrawCallBatch& SetPipelineState(const CPipelineStateObject& pipelineState)
		{
			pipelineStateDesc.m_PipelineStates = pipelineState;
			return *this;
		}

		inline DrawCallBatch& SetInputAssemblyStates(InputAssemblyStates assemblyStates)
		{
			pipelineStateDesc.m_InputAssemblyStates = assemblyStates;
			return *this;
		}

		inline DrawCallBatch& SetShaderSet(IShaderSet const* pShaderSet)
		{
			pipelineStateDesc.m_ShaderSet = pShaderSet;
			return *this;
		}

		inline DrawCallBatch& PushArgList(castl::string const& name, castl::shared_ptr<ShaderArgList> const& argList)
		{
			pipelineStateDesc.shaderArgLists.push_back(castl::make_pair(name, argList));
			return *this;
		}

		inline DrawCallBatch& PushArgList(castl::shared_ptr<ShaderArgList> const& argList)
		{
			return PushArgList("", argList);
		}

		inline DrawCallBatch& SetVertexBuffer(cacore::HashObj<VertexInputsDescriptor> const& vertexInputDesc, BufferHandle const& bufferHandle);
		inline DrawCallBatch& SetIndexBuffer(EIndexBufferType indexBufferType, BufferHandle const& bufferHandle, uint32_t byteOffset = 0);
		inline DrawCallBatch& Draw(castl::function<void(CommandList&)> commandFunc);
	};

	struct AttachmentConfig
	{
		EAttachmentLoadOp	loadOp;
		EAttachmentStoreOp	storeOp;
		GraphicsClearValue	clearValue;
		static AttachmentConfig Create(
			EAttachmentLoadOp loadOp = EAttachmentLoadOp::eLoad
			, EAttachmentStoreOp storeOp = EAttachmentStoreOp::eStore
			, GraphicsClearValue clearValue = GraphicsClearValue::ClearColor())
		{
			AttachmentConfig result{};
			result.loadOp = loadOp;
			result.storeOp = storeOp;
			result.clearValue = clearValue;
			return result;
		}
		static AttachmentConfig Clear(GraphicsClearValue const& clearValue = GraphicsClearValue::ClearColor()
			, EAttachmentStoreOp storeOp = EAttachmentStoreOp::eStore)
		{
			return Create(EAttachmentLoadOp::eClear, storeOp, clearValue);
		}
		static AttachmentConfig ClearDepthStencil(float depth = 1.0f, uint32_t stencil = 0
			, EAttachmentStoreOp storeOp = EAttachmentStoreOp::eStore)
		{
			return Create(EAttachmentLoadOp::eClear, storeOp, GraphicsClearValue::ClearDepthStencil(depth, stencil));
		}
	};

	class RenderPass
	{
	public:
		RenderPass() = default;
		static RenderPass New(ImageHandle const& color
			, EAttachmentLoadOp loadOp = EAttachmentLoadOp::eClear
			, EAttachmentStoreOp storeOp = EAttachmentStoreOp::eStore
			, GraphicsClearValue clearValue = {})
		{
			RenderPass pass{};
			pass.m_Arrachments = { color };
			pass.m_AttachmentConfigs = { AttachmentConfig::Create(loadOp, storeOp, clearValue) };
			pass.m_DepthAttachmentIndex = INVALID_ATTACHMENT_INDEX;
			pass.m_AttachmentLoadOp = loadOp;
			pass.m_AttachmentStoreOp = storeOp;
			pass.m_ClearValue = clearValue;
			return pass;
		}
		static RenderPass New(castl::vector<ImageHandle> const& colors, ImageHandle const& depth)
		{
			RenderPass pass{};
			pass.m_Arrachments = colors;
			pass.m_Arrachments.push_back(depth);
			pass.m_DepthAttachmentIndex = pass.m_Arrachments.size() - 1;
			return pass;
		}
		static RenderPass New(castl::vector<ImageHandle> const& colors)
		{
			RenderPass pass{};
			pass.m_Arrachments = colors;
			pass.m_AttachmentConfigs.resize(colors.size());
			castl::fill(pass.m_AttachmentConfigs.begin(), pass.m_AttachmentConfigs.end(), AttachmentConfig::Create());
			pass.m_DepthAttachmentIndex = INVALID_ATTACHMENT_INDEX;
			return pass;
		}
		static RenderPass New(ImageHandle const& color, ImageHandle const& depth
			, AttachmentConfig const& colorAttachmentConfig = AttachmentConfig::Create()
			, AttachmentConfig const& depthAttachmentConfig = AttachmentConfig::Create())
		{
			RenderPass pass{};
			pass.m_Arrachments = { color, depth };
			pass.m_AttachmentConfigs = { colorAttachmentConfig, depthAttachmentConfig };
			pass.m_DepthAttachmentIndex = 1;
			return pass;
		}

		inline RenderPass& SetAttachmentConfig(uint32_t index, AttachmentConfig const& attachmentConfig)
		{
			m_AttachmentConfigs[index] = attachmentConfig;
		}
		inline RenderPass& SetPipelineState(const CPipelineStateObject& pipelineState);
		inline RenderPass& PushShaderArguments(castl::string const& name, castl::shared_ptr<ShaderArgList> const& shaderArguments);
		inline RenderPass& PushShaderArguments(castl::shared_ptr<ShaderArgList> const& shaderArguments)
		{
			return PushShaderArguments("", shaderArguments);
		}
		inline RenderPass& SetInputAssemblyStates(InputAssemblyStates assemblyStates);
		inline RenderPass& SetShaders(IShaderSet const* shaderSet);
		inline RenderPass& DrawCall(DrawCallBatch const& drawcall);

		castl::vector<DrawCallBatch> const& GetDrawCallBatches() const { return m_DrawCallBatches; }
		castl::vector<ImageHandle> const& GetAttachments() const { return m_Arrachments; }
		int GetDepthAttachmentIndex() const { return m_DepthAttachmentIndex; }

		AttachmentConfig const& GetAttachmentConfig(uint32_t attachmentID) const {
			return m_AttachmentConfigs[attachmentID];
		}

		//GraphicsClearValue const& GetClearValue() const { return m_ClearValue; }
		//EAttachmentLoadOp GetAttachmentLoadOp() const { return m_AttachmentLoadOp; }
		//EAttachmentStoreOp GetAttachmentStoreOp() const { return m_AttachmentStoreOp; }
		PipelineDescData const& GetPipelineStates() const { return m_PipelineStates; }
	private:
		PipelineDescData m_PipelineStates;
		EAttachmentLoadOp m_AttachmentLoadOp = EAttachmentLoadOp::eLoad;
		EAttachmentStoreOp m_AttachmentStoreOp = EAttachmentStoreOp::eStore;
		GraphicsClearValue m_ClearValue = {};
		castl::vector<AttachmentConfig> m_AttachmentConfigs;
		castl::vector<DrawCallBatch> m_DrawCallBatches;
		castl::vector<ImageHandle> m_Arrachments;
		uint32_t m_DepthAttachmentIndex = INVALID_ATTACHMENT_INDEX;
		friend class GPUGraph;
	};
#pragma endregion

#pragma region Compute Shader
	class ComputeBatch
	{
	public:
		struct ComputeDispatch
		{
			IShaderSet const* shader;
			castl::string kernelName;
			castl::vector<
				castl::pair<castl::string, castl::shared_ptr<ShaderArgList>>
			> shaderArgLists;
			uint32_t x;
			uint32_t y;
			uint32_t z;

			static ComputeDispatch Create(IShaderSet const* shader, castl::string_view const& kernelName, uint32_t x, uint32_t y, uint32_t z, castl::vector<castl::pair<castl::string, castl::shared_ptr<ShaderArgList>>> const& argLists)
			{
				ComputeDispatch dispatchStruct{};
				dispatchStruct.shader = shader;
				dispatchStruct.kernelName = kernelName;
				dispatchStruct.x = x;
				dispatchStruct.y = y;
				dispatchStruct.z = z;
				dispatchStruct.shaderArgLists = argLists;
				return dispatchStruct;
			}
		};
		static ComputeBatch New()
		{
			ComputeBatch newBatch{};
			return newBatch;
		}
		//Shader Args
		castl::vector<
			castl::pair<castl::string, castl::shared_ptr<ShaderArgList>>
		> shaderArgLists;
		//Dispatchs
		castl::vector<ComputeDispatch> dispatchs;
		ComputeBatch& PushArgList(castl::string name, castl::shared_ptr<ShaderArgList> const& argList)
		{
			shaderArgLists.push_back(castl::make_pair(name, argList));
			return *this;
		}
		ComputeBatch& Dispatch(IShaderSet const* shaderSet, castl::string_view const& kernelName, uint32_t x, uint32_t y, uint32_t z
			, castl::vector<
			castl::pair<castl::string, castl::shared_ptr<ShaderArgList>>
			> const& argLists = {})
		{
			bool valid = (shaderSet != nullptr) && (x > 0 && y > 0 && z > 0) && !kernelName.empty();
			CA_ASSERT(valid, "Invalid Compute Dispatch!");
			if (valid)
			{
				dispatchs.push_back(ComputeDispatch::Create(shaderSet, kernelName, x, y, z, argLists));
			}
			return *this;
		}
	private:
	};
#pragma endregion

	struct GPUDataTransfers
	{
		struct DataReference
		{
			void const* pData;
			uint64_t dataIndex;
			uint64_t dstOffset;
			uint64_t dataSize;
			bool copied;
			static DataReference Create(void const* pData, uint64_t dataIndex, uint64_t dstOffset, uint64_t dataSize, bool copied)
			{
				DataReference result{ pData, dataIndex, dstOffset, dataSize, copied };
				return result;
			}
		};
		castl::vector<castl::pair<ImageHandle, DataReference>> m_ImageDataUploads;
		castl::vector<castl::pair<BufferHandle, DataReference>> m_BufferDataUploads;
	};

	template <typename DescriptorType>
	class GraphResourceManager
	{
	public:
		void RegisterHandle(ResourceHandleKey const& handleKey, DescriptorType const& desc)
		{
			if (!handleKey.Valid())
				return;
			auto find = m_HandleNameToDesc.find(handleKey);
			if (find != m_HandleNameToDesc.end())
			{
				CA_LOG_ERR(castl::string("handleName ") + "aready allocated");
				return;
			};
			m_Descriptors.push_back(desc);
			m_HandleNameToDesc.insert(castl::make_pair(handleKey, m_Descriptors.size() - 1));
		}

		int32_t GetDescriptorIndex(ResourceHandleKey const& handleKey) const
		{
			if (!handleKey.Valid())
				return -1;
			auto find = m_HandleNameToDesc.find(handleKey);
			if (find == m_HandleNameToDesc.end())
			{
				CA_LOG_ERR(castl::string("handleName ") + "not found");
				return -1;
			};
			return find->second;
		}

		DescriptorType const* DescriptorIDToDescriptor(int32_t index) const
		{
			if (index < 0 || index >= m_Descriptors.size())
			{
				CA_LOG_ERR(castl::string("index ") + "out of range");
				return nullptr;
			};
			return &m_Descriptors[index];
		}

		DescriptorType const* GetDescriptor(ResourceHandleKey const& handleKey) const
		{
			if (!handleKey.Valid())
				return nullptr;
			auto find = m_HandleNameToDesc.find(handleKey);
			if (find == m_HandleNameToDesc.end())
			{
				CA_LOG_ERR(castl::string("handleName ") + "not found");
				return nullptr;
			};
			return &m_Descriptors[find->second];
		}
	private:
		castl::unordered_map<ResourceHandleKey, int32_t> m_HandleNameToDesc;
		castl::vector<DescriptorType> m_Descriptors;
	};

	class GPUGraph
	{
	public:
		enum class EGraphStageType
		{
			eRenderPass,
			eComputePass,
			eTransferPass
		};

		//Create a new render pass
		inline GPUGraph& AddPass(RenderPass const& renderPass);
		inline GPUGraph& AddPass(ComputeBatch const& computePass);
		//Data Transition
		inline GPUGraph& ScheduleData(ImageHandle const& imageHandle, void const* data, uint64_t size, uint64_t offset = 0);
		inline GPUGraph& ScheduleData(BufferHandle const& bufferHandle, void const* data, uint64_t size, uint64_t offset = 0);
		//Allocate a graph local image
		inline GPUGraph& AllocImage(ImageHandle const& imageHandle, GPUTextureDescriptor const& desc);
		//Allocate a graph local buffer
		inline GPUGraph& AllocBuffer(BufferHandle const& bufferHandle, GPUBufferDescriptor const& desc);

		castl::vector<EGraphStageType> const& GetGraphStages() const { return m_StageTypes; }
		castl::deque<RenderPass> const& GetRenderPasses() const { return m_RenderPasses; }
		castl::deque<ComputeBatch> const& GetComputePasses() const { return m_ComputePasses; }
		castl::vector<GPUDataTransfers> const& GetDataTransfers() const { return m_DataTransfers; }
		castl::vector<uint32_t> const& GetPassIndices() const { return m_PassIndices; }
		UploadDataHolder const& GetUploadDataHolder() const { return m_DataHolder; }
		GraphResourceManager<GPUTextureDescriptor> const& GetImageManager() const { return m_InternalImageManager; }
		GraphResourceManager<GPUBufferDescriptor> const& GetBufferManager() const { return m_InternalBufferManager; }
	private:
		//Render Passes
		castl::deque<RenderPass> m_RenderPasses;
		//Compute Passes
		castl::deque<ComputeBatch> m_ComputePasses;
		//Data Syncs
		castl::vector<GPUDataTransfers> m_DataTransfers;
		//Stages
		castl::vector<EGraphStageType> m_StageTypes;
		//Stage To Pass Index
		castl::vector<uint32_t> m_PassIndices;
		//Submit Data Holder
		UploadDataHolder m_DataHolder;
		//Internal Resources
		GraphResourceManager<GPUTextureDescriptor> m_InternalImageManager;
		GraphResourceManager<GPUBufferDescriptor> m_InternalBufferManager;
	};

	DrawCallBatch& DrawCallBatch::SetVertexBuffer(cacore::HashObj<VertexInputsDescriptor> const& vertexInputDesc
		, BufferHandle const& bufferHandle)
	{
		m_BoundVertexBuffers[vertexInputDesc] = bufferHandle;
		return *this;
	}

	DrawCallBatch& DrawCallBatch::SetIndexBuffer(EIndexBufferType indexBufferType, BufferHandle const& bufferHandle, uint32_t byteOffset)
	{
		m_BoundIndexBuffer = bufferHandle;
		m_IndexBufferType = indexBufferType;
		m_IndexBufferOffset = byteOffset;
		return *this;
	}
	DrawCallBatch& DrawCallBatch::Draw(castl::function<void(CommandList&)> commandFunc)
	{
		m_DrawCommands.push_back(commandFunc);
		return *this;
	}

	RenderPass& RenderPass::SetPipelineState(const CPipelineStateObject& pipelineState)
	{
		m_PipelineStates.m_PipelineStates = pipelineState;
		return *this;
	}
	RenderPass& RenderPass::SetInputAssemblyStates(InputAssemblyStates assemblyStates)
	{
		m_PipelineStates.m_InputAssemblyStates = assemblyStates;
		return *this;
	}

	RenderPass& RenderPass::PushShaderArguments(castl::string const& name, castl::shared_ptr<ShaderArgList> const& shaderArguments)
	{
		m_PipelineStates.shaderArgLists.push_back(castl::make_pair(name, shaderArguments));
		return *this;
	}

	RenderPass& RenderPass::SetShaders(IShaderSet const* shaderSet)
	{
		m_PipelineStates.m_ShaderSet = shaderSet;
		return *this;
	}

	RenderPass& RenderPass::DrawCall(DrawCallBatch const& drawcall)
	{
		m_DrawCallBatches.push_back(drawcall);
		return *this;
	}

	GPUGraph& GPUGraph::AddPass(RenderPass const& renderPass)
	{
		m_StageTypes.push_back(EGraphStageType::eRenderPass);
		m_PassIndices.push_back(m_RenderPasses.size());
		m_RenderPasses.push_back(renderPass);
		return *this;
	}

	GPUGraph& GPUGraph::AddPass(ComputeBatch const& computePass)
	{
		m_StageTypes.push_back(EGraphStageType::eComputePass);
		m_PassIndices.push_back(m_ComputePasses.size());
		m_ComputePasses.push_back(computePass);
		return *this;
	}
	
	GPUGraph& GPUGraph::ScheduleData(ImageHandle const& imageHandle, void const* data, uint64_t size, uint64_t offset)
	{
		if (m_StageTypes.empty() || m_StageTypes.back() != EGraphStageType::eTransferPass)
		{
			m_StageTypes.push_back(EGraphStageType::eTransferPass);
			m_PassIndices.push_back(m_DataTransfers.size());
		}
		if (m_DataTransfers.empty())
		{
			m_DataTransfers.emplace_back();
		}
		uint64_t dataIndex = m_DataHolder.AddData(data, size);
		m_DataTransfers.back().m_ImageDataUploads.push_back(castl::make_pair( imageHandle
			, GPUDataTransfers::DataReference::Create(data, dataIndex, offset, size, true) ));
		return *this;
	}
	GPUGraph& GPUGraph::ScheduleData(BufferHandle const& bufferHandle, void const* data, uint64_t size, uint64_t offset)
	{
		if (m_StageTypes.empty() || m_StageTypes.back() != EGraphStageType::eTransferPass)
		{
			m_StageTypes.push_back(EGraphStageType::eTransferPass);
			m_PassIndices.push_back(m_DataTransfers.size());
		}
		if (m_DataTransfers.empty())
		{
			m_DataTransfers.emplace_back();
		}
		uint64_t dataIndex = m_DataHolder.AddData(data, size);
		m_DataTransfers.back().m_BufferDataUploads.push_back(castl::make_pair(bufferHandle
			, GPUDataTransfers::DataReference::Create(data, dataIndex, offset, size, true)));
		return *this;
	}
	GPUGraph& GPUGraph::AllocImage(ImageHandle const& imageHandle, GPUTextureDescriptor const& desc)
	{
		if (imageHandle.GetType() != ImageHandle::ImageType::Internal)
			return *this;
		m_InternalImageManager.RegisterHandle(imageHandle.GetKey(), desc);
		return *this;
	}
	GPUGraph& GPUGraph::AllocBuffer(BufferHandle const& bufferHandle, GPUBufferDescriptor const& desc)
	{
		if (bufferHandle.GetType() != BufferHandle::BufferType::Internal)
			return *this;
		m_InternalBufferManager.RegisterHandle(bufferHandle.GetKey(), desc);
		return *this;
	}

}