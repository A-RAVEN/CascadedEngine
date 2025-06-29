#pragma once
#include "Common.h"
#include "CCommandList.h"
#include "ShaderArgList.h"
#include "CPipelineStateObject.h"
#include "ShaderProvider.h"
#include "ShaderResourceHandle.h"
#include <DebugUtils.h>
#include <CASTL/CAFunctional.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAMap.h>
#include <CASTL/CADeque.h>
#include <ShaderStruct.h>
#include <CASTL/CAStringView.h>
#include <CVertexInputDescriptor.h>

namespace graphics_backend
{
	//using VertexInputBufferMap = castl::unordered_map<cacore::HashObj<VertexInputsDescriptor>, BufferHandle>;
	using ShaderStructDic = castl::unordered_map<cacore::NameHash, castl::shared_ptr<ShaderStruct>>;
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
		//IShaderSet const* m_ShaderSet;
		ShaderInfo m_ShaderInfo;
		cacore::HashObj<CPipelineStateObject> m_PipelineStates;
		cacore::HashObj<InputAssemblyStates> m_InputAssemblyStates;
		cacore::HashObj<RectSate> m_Viewport;
		cacore::HashObj<RectSate> m_Scissor;
		//TODO: Do Not Expose This
		static PipelineDescData CombindDescData(PipelineDescData const& parent, PipelineDescData const& child)
		{
			PipelineDescData newDescData{};
			newDescData.m_Viewport = child.m_Viewport.Valid() ? child.m_Viewport : parent.m_Viewport;
			newDescData.m_Scissor = child.m_Scissor.Valid() ? child.m_Scissor : parent.m_Scissor;
			newDescData.m_ShaderInfo = child.m_ShaderInfo.isValid() ? child.m_ShaderInfo : parent.m_ShaderInfo;
			newDescData.m_PipelineStates = child.m_PipelineStates.Valid() ? child.m_PipelineStates : parent.m_PipelineStates;
			newDescData.m_InputAssemblyStates = child.m_InputAssemblyStates.Valid() ? child.m_InputAssemblyStates : parent.m_InputAssemblyStates;
			return newDescData;
		}
	};

	struct IndexBufferData
	{
		BufferHandle indexBufferHandle;
		EIndexBufferType indexBufferType = EIndexBufferType::e16;
		uint32_t indexBufferOffset = 0;
		IndexBufferData const& Select(IndexBufferData const& other)
		{
			if (indexBufferHandle.IsValid())
				return *this;
			return other;
		}
		bool IsValid() const { return indexBufferHandle.IsValid(); }
	};

	struct ViewRectData
	{
		int x;
		int y;
		int width;
		int height;
		auto operator<=> (const ViewRectData&) const = default;
	};

	class DrawCall
	{
	public:
		struct DrawInfo
		{
			bool drawIndexed = false;

			uint32_t indexOffset = 0;
			uint32_t indexCount = 0;

			uint32_t vertexOffset = 0;
			uint32_t vertexCount = 0;

			uint32_t firstInstanceID = 0;
			uint32_t instanceCount = 0;
		};

		static DrawCall New()
		{
			return {};
		}

		inline DrawCall& SetVertexBuffer(cacore::NameHash const& name, BufferHandle const& bufferHandle)
		{
			m_BoundVertexBuffers[name] = bufferHandle;
			return *this;
		}
		inline DrawCall& SetIndexBuffer(EIndexBufferType indexBufferType, BufferHandle const& bufferHandle, uint32_t byteOffset = 0)
		{
			m_IndexBufferData = { bufferHandle, indexBufferType, byteOffset };
			return *this;
		}
		inline DrawCall& DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t indexOffset = 0, uint32_t vertexOffset = 0, uint32_t firstInstance = 0)
		{
			m_DrawInfo = DrawInfo{ true, indexOffset, indexCount, vertexOffset, 0, firstInstance, instanceCount };
			return *this;
		}
		inline DrawCall& Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t vertexOffset = 0, uint32_t firstInstance = 0)
		{
			m_DrawInfo = DrawInfo{ false, 0, 0, vertexOffset, vertexCount, firstInstance, instanceCount };
			return *this;
		}

		inline DrawCall& ViewPort(int x, int y, int width, int height)
		{
			m_ViewPort = ViewRectData{ x, y, width, height };
			return *this;
		}

		inline DrawCall& Scissor(int x, int y, int width, int height)
		{
			m_Sissor = ViewRectData{ x, y, width, height };
			return *this;
		}

		DrawInfo const& GetDrawInfo() const { return m_DrawInfo; }
		IndexBufferData const& GetIndexBuffer() const { return m_IndexBufferData; }
		castl::unordered_map<cacore::NameHash, BufferHandle> const& GetVertexBuffers() const { return m_BoundVertexBuffers; }
		cacore::HashObj<ViewRectData> const& GetViewPort() const { return m_ViewPort; }
		cacore::HashObj<ViewRectData> const& GetScissor() const { return m_Sissor; }
	private:
		//PipelineDescData m_PipelineStateDesc;
		castl::unordered_map<cacore::NameHash, BufferHandle> m_BoundVertexBuffers;
		IndexBufferData m_IndexBufferData;
		DrawInfo m_DrawInfo;
		cacore::HashObj<ViewRectData> m_ViewPort;
		cacore::HashObj<ViewRectData> m_Sissor;
	};

	class DrawCallBatch
	{
	public:
		using VertexInputDescMap = castl::unordered_map<cacore::NameHash, cacore::HashObj<VertexInputsDescriptor>>;
		static DrawCallBatch New()
		{
			return {};
		}

		//PSO
		PipelineDescData pipelineStateDesc;
		//Draw Calls
		castl::vector<DrawCall> m_DrawCalls;
		ShaderStructDic shaderStructs;
		VertexInputDescMap m_VertexInputDescs;

		PipelineDescData const& GetPipelineStates() const
		{
			return pipelineStateDesc;
		}

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

		inline DrawCallBatch& SetShaderInfo(ShaderInfo const& shaderInfo)
		{
			pipelineStateDesc.m_ShaderInfo = shaderInfo;
			return *this;
		}

		inline DrawCallBatch& SetViewPort(RectSate const& viewport)
		{
			pipelineStateDesc.m_Viewport = viewport;
			return *this;
		}

		inline DrawCallBatch& SetScissor(RectSate const& scissor)
		{
			pipelineStateDesc.m_Scissor = scissor;
			return *this;
		}

		inline DrawCallBatch& SetParam(cacore::NameHash const& name, castl::shared_ptr<ShaderStruct> const& shaderStruct)
		{
			CA_ASSERT_BREAK(shaderStruct != nullptr, "Shader Struct Is Null When Setting Param:{}", name);
			shaderStructs[name] = shaderStruct;
			return *this;
		}

		inline DrawCallBatch& VertexStream(cacore::NameHash const& name, cacore::HashObj<VertexInputsDescriptor> const& vertexInputDesc)
		{
			m_VertexInputDescs[name] = vertexInputDesc;
			return *this;
		}

		//inline DrawCallBatch& SetIndexBuffer(EIndexBufferType indexBufferType, BufferHandle const& bufferHandle, uint32_t byteOffset = 0);
		//inline DrawCallBatch& Draw(castl::function<void(CommandList&)> commandFunc);
		inline DrawCallBatch& DrawCall(DrawCall const& drawcall)
		{
			m_DrawCalls.push_back(drawcall);
			return *this;
		}
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
			, AttachmentConfig const& colorAttachmentConfig = AttachmentConfig::Create())
		{
			RenderPass pass{};
			pass.m_Arrachments = { color };
			pass.m_AttachmentConfigs = { colorAttachmentConfig };
			pass.m_DepthAttachmentIndex = INVALID_ATTACHMENT_INDEX;
			pass.SetPipelineState({});
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

		RenderPass& Name(cacore::NameHash const& name)
		{
			m_Name = name;
			return *this;
		}

		inline RenderPass& SetAttachmentConfig(uint32_t index, AttachmentConfig const& attachmentConfig)
		{
			m_AttachmentConfigs[index] = attachmentConfig;
		}
		inline RenderPass& SetPipelineState(const CPipelineStateObject& pipelineState);
		inline RenderPass& SetParam(cacore::NameHash const& name, castl::shared_ptr<ShaderStruct> const& shaderStruct);
		inline RenderPass& SetInputAssemblyStates(InputAssemblyStates assemblyStates);
		inline RenderPass& SetShaderInfo(ShaderInfo const& shaderInfo);
		inline RenderPass& DrawCall(DrawCallBatch const& drawcall);

		castl::vector<DrawCallBatch> const& GetDrawCallBatches() const { return m_DrawCallBatches; }
		castl::vector<ImageHandle> const& GetAttachments() const { return m_Arrachments; }
		int GetDepthAttachmentIndex() const { return m_DepthAttachmentIndex; }
		bool HasDepthAttachment() const
		{
			return m_DepthAttachmentIndex != INVALID_ATTACHMENT_INDEX;
		}

		AttachmentConfig const& GetAttachmentConfig(uint32_t attachmentID) const {
			return m_AttachmentConfigs[attachmentID];
		}

		PipelineDescData const& GetPipelineStates() const { return m_PipelineStates; }

		castl::unordered_map<cacore::NameHash, castl::shared_ptr<ShaderStruct>> const& GetShaderStructs() const { return shaderStructs; }

		cacore::NameHash const& GetName() const { return m_Name; }
	private:
		PipelineDescData m_PipelineStates;
		ShaderStructDic shaderStructs;
		castl::vector<AttachmentConfig> m_AttachmentConfigs;
		castl::vector<DrawCallBatch> m_DrawCallBatches;
		castl::vector<ImageHandle> m_Arrachments;
		uint32_t m_DepthAttachmentIndex = INVALID_ATTACHMENT_INDEX;
		cacore::NameHash m_Name;
		friend class GPUGraph;
	};
#pragma endregion

#pragma region Compute Shader
	class ComputeBatch
	{
	public:
		struct ComputeDispatch
		{
			ShaderInfo m_ShaderInfo;
			castl::string kernelName;
			//castl::vector<
			//	castl::pair<castl::string, castl::shared_ptr<ShaderArgList>>
			//> shaderArgLists;
			ShaderStructDic shaderStructs;
			uint32_t x;
			uint32_t y;
			uint32_t z;

			static ComputeDispatch Create(ShaderInfo const& shader, castl::string_view const& kernelName, uint32_t x, uint32_t y, uint32_t z, ShaderStructDic const& shaderStructs)
			{
				ComputeDispatch dispatchStruct{};
				dispatchStruct.m_ShaderInfo = shader;
				dispatchStruct.kernelName = kernelName;
				dispatchStruct.x = x;
				dispatchStruct.y = y;
				dispatchStruct.z = z;
				dispatchStruct.shaderStructs = shaderStructs;
				return dispatchStruct;
			}
		};
		static ComputeBatch New()
		{
			ComputeBatch newBatch{};
			return newBatch;
		}
		//Shader Args
		//castl::vector<
		//	castl::pair<castl::string, castl::shared_ptr<ShaderArgList>>
		//> shaderArgLists;
		ShaderStructDic shaderStructs;
		//Dispatchs
		castl::vector<ComputeDispatch> dispatchs;

		ComputeBatch& SetParam(cacore::NameHash const& name, castl::shared_ptr<ShaderStruct> const& shaderStruct)
		{
			shaderStructs[name] = shaderStruct;
			return *this;
		}

		//ComputeBatch& PushArgList(castl::string name, castl::shared_ptr<ShaderArgList> const& argList)
		//{
		//	shaderArgLists.push_back(castl::make_pair(name, argList));
		//	return *this;
		//}
		ComputeBatch& Dispatch(ShaderInfo const& shaderSet, castl::string_view const& kernelName, uint32_t x, uint32_t y, uint32_t z
			, ShaderStructDic const& shaderStructs = {})
		{
			bool valid = (shaderSet.isValid()) && (x > 0 && y > 0 && z > 0) && !kernelName.empty();
			CA_ASSERT(valid, "Invalid Compute Dispatch!");
			if (valid)
			{
				dispatchs.push_back(ComputeDispatch::Create(shaderSet, kernelName, x, y, z, shaderStructs));
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
				CA_LOG_ERR("handle {} already allocated!", handleKey->name.Get());
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
				CA_LOG_ERR_BREAK("handle {} not found!", handleKey->name.Get());
				return -1;
			};
			return find->second;
		}

		DescriptorType const* DescriptorIDToDescriptor(int32_t index) const
		{
			if (index < 0 || index >= m_Descriptors.size())
			{
				CA_LOG_ERR("DescriptorID {} out of range!", index);
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
				CA_LOG_ERR("handle {} not found!", handleKey->name.Get());
				return nullptr;
			};
			return &m_Descriptors[find->second];
		}

		castl::unordered_map<ResourceHandleKey, int32_t> const& GetHandleNameToDesc() const
		{
			return m_HandleNameToDesc;
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
			eTransferPass,
			eSubGraph
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
		inline GPUGraph& Present(ImageHandle const& windowHandle)
		{
			if (windowHandle.GetType() == ImageHandle::ImageType::Backbuffer)
			{
				m_PresentBackBuffers.push_back(windowHandle);
			}
			return *this;
		}
		inline GPUGraph& SubGraph(castl::shared_ptr<GPUGraph> const& subGraph)
		{
			m_StageTypes.push_back(EGraphStageType::eSubGraph);
			m_PassIndices.push_back(m_SubGraphs.size());
			m_SubGraphs.push_back(subGraph);
			return *this;
		}
		castl::vector<EGraphStageType> const& GetGraphStages() const { return m_StageTypes; }
		castl::deque<RenderPass> const& GetRenderPasses() const { return m_RenderPasses; }
		castl::deque<ComputeBatch> const& GetComputePasses() const { return m_ComputePasses; }
		castl::vector<GPUDataTransfers> const& GetDataTransfers() const { return m_DataTransfers; }
		castl::vector<uint32_t> const& GetPassIndices() const { return m_PassIndices; }
		UploadDataHolder const& GetUploadDataHolder() const { return m_DataHolder; }
		GraphResourceManager<GPUTextureDescriptor> const& GetImageManager() const { return m_InternalImageManager; }
		GraphResourceManager<GPUBufferDescriptor> const& GetBufferManager() const { return m_InternalBufferManager; }
		castl::vector<ImageHandle> const& GetPresentBackBuffers() const { return m_PresentBackBuffers; }
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
		castl::vector<ImageHandle> m_PresentBackBuffers;
		castl::vector<castl::shared_ptr<GPUGraph>> m_SubGraphs;
	};

	template<typename TSS, typename TSSRange>
	static void ForeachShaderStructs(TSSRange const& inShaderStructRange, castl::function<void(TSS const&)> callback)
	{
		castl::deque<castl::shared_ptr<ShaderStruct>> shaderStructs;
		for (castl::shared_ptr<ShaderStruct> const& shaderStruct : inShaderStructRange)
		{
			CA_ASSERT_BREAK(shaderStruct.second != nullptr, "Shader Struct Is Null, Why!?");
			shaderStructs.push_back(shaderStruct.second);
		}
		/*auto& drawcallBatchs = renderPass.GetDrawCallBatches();
		for (auto& batch : drawcallBatchs)
		{
			for (auto shaderStruct : batch.shaderStructs)
			{
				CA_ASSERT_BREAK(shaderStruct.second != nullptr, "Shader Struct Is Null, Why!?");
				shaderStructs.push_back(shaderStruct.second);
			}
		}*/
		while (!shaderStructs.empty())
		{
			auto shaderStruct = shaderStructs.front();
			TSS* pStruct = static_cast<TSS*>(shaderStruct.get());
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

	//RenderPass& RenderPass::PushShaderArguments(castl::string const& name, castl::shared_ptr<ShaderArgList> const& shaderArguments)
	//{
	//	//m_PipelineStates.shaderArgLists.push_back(castl::make_pair(name, shaderArguments));
	//	return *this;
	//}

	inline RenderPass& RenderPass::SetParam(cacore::NameHash const& name, castl::shared_ptr<ShaderStruct> const& shaderStruct)
	{
		shaderStructs[name] = shaderStruct;
		return *this;
	}

	//RenderPass& RenderPass::SetShaders(IShaderSet const* shaderSet)
	//{
	//	m_PipelineStates.m_ShaderSet = shaderSet;
	//	return *this;
	//}

	RenderPass& RenderPass::SetShaderInfo(ShaderInfo const& shaderInfo)
	{
		m_PipelineStates.m_ShaderInfo = shaderInfo;
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
		m_DataTransfers.back().m_ImageDataUploads.push_back(castl::make_pair(imageHandle
			, GPUDataTransfers::DataReference::Create(data, dataIndex, offset, size, true)));
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