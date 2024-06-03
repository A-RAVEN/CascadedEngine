#pragma once
#include <CASTL/CASharedPtr.h>
#include <GPUGraph.h>
#include <VulkanApplicationSubobjectBase.h>
#include <VulkanImageObject.h>
#include <InterfaceTranslator.h>
#include <VulkanBarrierCollector.h>
#include "ShaderBindingHolder.h"
#include <GPUContexts/FrameContext.h>

namespace graphics_backend
{
	class FramebufferObject;
	class RenderPassObject;
	class CPipelineObject;
	class CVulkanApplication;

	struct VertexAttributeBindingData
	{
		uint32_t bindingIndex;
		uint32_t stride;
		castl::vector<VertexAttribute> attributes;
		bool bInstance;
	};

	struct GPUPassBatchInfo
	{
		castl::shared_ptr<CPipelineObject> m_PSO;
		castl::unordered_map<castl::string, VertexAttributeBindingData> m_VertexAttributeBindings;
		ShaderBindingInstance m_ShaderBindingInstance;
	};

	struct PassInfoBase
	{
		VulkanBarrierCollector m_BarrierCollector;
		castl::set<uint32_t> m_PredecessorPasses;
		castl::set<uint32_t> m_SuccessorPasses;
		castl::set<uint32_t> m_WaitingQueueFamilies;
		int GetQueueFamily() const { return m_BarrierCollector.GetQueueFamily(); }
	};

	struct GPUPassInfo : public PassInfoBase
	{
		castl::shared_ptr<FramebufferObject> m_FrameBufferObject;
		castl::shared_ptr<RenderPassObject> m_RenderPassObject;
		castl::vector<vk::ClearValue> m_ClearValues;
		castl::vector<GPUPassBatchInfo> m_Batches;
	};

	struct GPUTransferInfo : public PassInfoBase
	{
	};

	class SubAllocator
	{
	public:
		uint32_t passAllocationCount;
		uint32_t AllocIndex()
		{
			if (m_AvailableIndices.empty())
			{
				++passAllocationCount;
				return passAllocationCount - 1;
			}
			uint32_t result = m_AvailableIndices.front();
			m_AvailableIndices.pop_front();
			return result;
		}

		void ReturnIndex(uint32_t index)
		{
			CA_ASSERT(index < passAllocationCount, "Return Invalid Allocation Index");
			m_AvailableIndices.push_back(index);
		}

		virtual void Release()
		{
			passAllocationCount = 0;
			m_AvailableIndices.clear();
		}

		castl::deque<uint32_t> m_AvailableIndices;
	};

	struct ResourceInfo
	{
		int32_t descriptorIndex;
		uint32_t beginPass;
		uint32_t endPass;

		ResourceInfo(int32_t descIndex, uint32_t passID)
			: descriptorIndex(descIndex)
			, beginPass(passID)
			, endPass(passID)
		{}

		void UpdateEndPass(uint32_t passID)
		{
			endPass = castl::max(endPass, passID);
		}
	};

	template<typename SubAllocator, typename ResManager>
	class GraphExecutorResourceManager
	{
	public:
		void AllocResourceIndex(castl::string const& handleName, int32_t descriptorIndex)
		{
			auto found = m_HandleNameToResourceInfo.find(handleName);
			if (found == m_HandleNameToResourceInfo.end())
			{
				m_HandleNameToResourceInfo.insert(castl::make_pair(handleName, ResourceInfo(descriptorIndex, m_PassCount)));
			}
			else
			{
				found->second.UpdateEndPass(m_PassCount);
			}
		}

		void ResetAllocator()
		{
			m_PassCount = 0;
		}

		void NextPass()
		{
			++m_PassCount;
		}

		void AllocateResources(CVulkanApplication& app, FrameBoundResourcePool* pResourcePool, ResManager const& bufferHandleManager)
		{
			castl::vector<castl::vector<castl::pair<castl::string, int32_t>>> passAllocations;
			castl::vector<castl::vector<castl::pair<castl::string, int32_t>>> passDeAllocations;

			passAllocations.resize(m_PassCount);
			passDeAllocations.resize(m_PassCount);

			for (auto& lifeTimePair : m_HandleNameToResourceInfo)
			{
				passAllocations[lifeTimePair.second.beginPass].push_back(castl::make_pair(lifeTimePair.first, lifeTimePair.second.descriptorIndex));
				if (lifeTimePair.second.endPass < m_PassCount - 1)
					passDeAllocations[lifeTimePair.second.endPass + 1].push_back(castl::make_pair(lifeTimePair.first, lifeTimePair.second.descriptorIndex));
			}

			for (uint32_t passID = 0; passID < m_PassCount; ++passID)
			{
				auto& passDeAllocationList = passDeAllocations[passID];
				for (auto& allocRes : passDeAllocationList)
				{
					auto allocationData = m_HandleNameToResourceIndex.find(allocRes.first);
					CA_ASSERT(allocationData != m_HandleNameToResourceIndex.end(), "Allocation Not Found");
					uint32_t subAllocatorIndex = GetSubAllocatorIndex(allocRes.second);
					CA_ASSERT(allocationData->second.first == subAllocatorIndex, "Allocation Desc Match");
					m_SubAllocators[subAllocatorIndex].ReturnIndex(allocationData->second.second);
				}

				auto& passAllocationList = passAllocations[passID];
				for (auto& allocRes : passAllocationList)
				{
					uint32_t subAllocatorIndex = GetSubAllocatorIndex(allocRes.second);
					uint32_t allocatedIndex = m_SubAllocators[subAllocatorIndex].AllocIndex();
					m_HandleNameToResourceIndex.insert(castl::make_pair(allocRes.first, castl::make_pair(subAllocatorIndex, allocatedIndex)));
				}
			}

			for (auto& descAllocatorPair : m_DescriptorIndexToSubAllocator)
			{
				auto desc = bufferHandleManager.DescriptorIDToDescriptor(descAllocatorPair.first);
				CA_ASSERT(desc != nullptr, "Descriptor not found");
				m_SubAllocators[descAllocatorPair.second].Allocate(app, pResourcePool, *desc);
			}
		}

		void ReleaseAll()
		{
			for (auto& allocator : m_SubAllocators)
			{
				allocator.Release();
			}
			m_SubAllocators.clear();
			m_DescriptorIndexToSubAllocator.clear();
			m_HandleNameToResourceIndex.clear();
		}

	protected:
		uint32_t GetSubAllocatorIndex(int32_t descIndex)
		{
			auto found = m_DescriptorIndexToSubAllocator.find(descIndex);
			if (found == m_DescriptorIndexToSubAllocator.end())
			{
				found = m_DescriptorIndexToSubAllocator.insert(castl::make_pair(descIndex, m_SubAllocators.size())).first;
				m_SubAllocators.emplace_back();
			}
			return found->second;
		}

		uint32_t m_PassCount;
		castl::vector<SubAllocator> m_SubAllocators;
		castl::unordered_map<int32_t, uint32_t> m_DescriptorIndexToSubAllocator;
		castl::unordered_map<castl::string, castl::pair<uint32_t, uint32_t>> m_HandleNameToResourceIndex;
		castl::unordered_map<castl::string, ResourceInfo> m_HandleNameToResourceInfo;
	};


	class BufferSubAllocator : public SubAllocator
	{
	public:
		void Allocate(CVulkanApplication& app, FrameBoundResourcePool* pResourcePool, GPUBufferDescriptor const& descriptor);

		virtual void Release()
		{
			SubAllocator::Release();
			m_Buffers.clear();
		}

		castl::vector<VKBufferObject> m_Buffers;
	};

	class GraphExecutorBufferManager : public GraphExecutorResourceManager<BufferSubAllocator, GraphResourceManager<GPUBufferDescriptor>>
	{
	public:
		VKBufferObject const& GetBufferObject(castl::string const& handleName) const
		{
			auto found = m_HandleNameToResourceIndex.find(handleName);
			if (found == m_HandleNameToResourceIndex.end())
			{
				return VKBufferObject::Default();
			}
			auto& subAllocator = m_SubAllocators[found->second.first];
			return subAllocator.m_Buffers[found->second.second];
		}
	};


	class ImageSubAllocator : public SubAllocator
	{
	public:
		void Allocate(CVulkanApplication& app, FrameBoundResourcePool* pResourcePool, GPUTextureDescriptor const& descriptor);

		virtual void Release()
		{
			SubAllocator::Release();
			m_Images.clear();
		}

		castl::vector<VKImageObject> m_Images;
	};

	class GraphExecutorImageManager : public GraphExecutorResourceManager<ImageSubAllocator, GraphResourceManager<GPUTextureDescriptor>>
	{
	public:
		VKImageObject const& GetImageObject(castl::string const& handleName) const
		{
			auto found = m_HandleNameToResourceIndex.find(handleName);
			if (found == m_HandleNameToResourceIndex.end())
			{
				return VKImageObject::Default();
			}
			auto& subAllocator = m_SubAllocators[found->second.first];
			return subAllocator.m_Images[found->second.second];
		}
	};

	struct CommandBatchRange
	{
		uint32_t queueFamilyIndex;
		uint32_t firstPass;
		uint32_t lastPass;
		vk::Semaphore signalSemaphore;
		castl::vector<vk::Semaphore> waitSemaphores;
		castl::vector<vk::PipelineStageFlags> waitStages;

		bool hasSuccessor;
		castl::set<uint32_t> waitingBatch;
		castl::set<uint32_t> waitingQueueFamilyReleaser;

		static CommandBatchRange Create(uint32_t queueFamilyIndex, uint32_t startPassID)
		{
			CommandBatchRange result{};
			result.queueFamilyIndex = queueFamilyIndex;
			result.firstPass = result.lastPass = startPassID;
			result.hasSuccessor = false;
			return result;
		}
	};

	struct ResourceState
	{
		int passID;
		ResourceUsageFlags usage;
		uint32_t queueFamily;
	};

	class GPUGraphExecutor : public VKAppSubObjectBaseNoCopy, public ShadderResourceProvider
	{
	public:
		GPUGraphExecutor(CVulkanApplication& application);
		void Initialize(castl::shared_ptr<GPUGraph> const& gpuGraph, FrameBoundResourcePool* frameBoundResourceManager);
		void Release();
		void PrepareGraph();
	private:
		void PrepareResources();

		void PrepareVertexBuffersBarriers(VulkanBarrierCollector& inoutBarrierCollector
			, castl::unordered_map<vk::Buffer, ResourceState>& inoutBufferUsageFlagCache
			, DrawCallBatch const& batch
			, GPUPassBatchInfo const& batchInfo
			, uint32_t passID
		);

		void PrepareShaderArgsResourceBarriers(VulkanBarrierCollector& inoutBarrierCollector
			, castl::unordered_map<vk::Image, ResourceState>& inoutImageUsageFlagCache
			, castl::unordered_map<vk::Buffer, ResourceState>& inoutBufferUsageFlagCache
			, ShaderArgList const* shaderArgList
			, uint32_t passID
		);
		VulkanBarrierCollector& GetBarrierCollector(uint32_t passID);
		PassInfoBase* GetBasePassInfo(int passID);

#pragma region Shader Resource Dependencies
		void UpdateBufferDependency(uint32_t passID, BufferHandle const& bufferHandle
			, ResourceUsageFlags newUsageFlags
			, castl::unordered_map<vk::Buffer, ResourceState>& inoutBufferUsageFlagCache);
		void UpdateImageDependency(uint32_t passID, ImageHandle const& imageHandle
			, ResourceUsageFlags newUsageFlags
			, castl::unordered_map<vk::Image, ResourceState>& inoutImageUsageFlagCache);
#pragma endregion
		void PrepareFrameBufferAndPSOs();
		void PrepareResourceBarriers();
		void RecordGraph();
		void ScanCommandBatchs();
		void Submit();
		void SyncExternalResources();

		GPUTextureDescriptor const* GetTextureHandleDescriptor(ImageHandle const& handle) const;
		vk::ImageView GetTextureHandleImageView(ImageHandle const& handle, GPUTextureView const& view) const;
		vk::Image GetTextureHandleImageObject(ImageHandle const& handle) const;
		vk::Buffer GetBufferHandleBufferObject(BufferHandle const& handle) const;

		virtual vk::Buffer GetBufferFromHandle(BufferHandle const& handle) override { return GetBufferHandleBufferObject(handle); }
		virtual vk::ImageView GetImageView(ImageHandle const& handle, GPUTextureView const& view) override { return GetTextureHandleImageView(handle, view);  }
	
		castl::vector<vk::CommandBuffer> const& GetCommandBufferList() const { return m_CommandBuffers; }
		castl::vector<CommandBatchRange> const& GetCommandBufferBatchList() const { return m_CommandBufferBatchList; }

		void UpdateExternalBufferUsage(PassInfoBase* passInfo, BufferHandle const& handle, ResourceState const& initUsageState, ResourceState const& newUsageState);
		void UpdateExternalImageUsage(PassInfoBase* passInfo, ImageHandle const& handle, ResourceState const& initUsageState, ResourceState const& newUsageState);
	private:
		struct ExternalResourceReleaser
		{
			VulkanBarrierCollector barrierCollector;
			vk::CommandBuffer commandBuffer;
			vk::Semaphore signalSemaphore;
		};

		struct ExternalResourceReleasingBarriers
		{
			castl::unordered_map<uint32_t, ExternalResourceReleaser> queueFamilyToBarrierCollector;
			ExternalResourceReleaser& GetQueueFamilyReleaser(uint32_t queueFamily)
			{
				auto found = queueFamilyToBarrierCollector.find(queueFamily);
				if (found == queueFamilyToBarrierCollector.end())
				{
					ExternalResourceReleaser newReleaser{};
					newReleaser.barrierCollector = VulkanBarrierCollector{ queueFamily };
					newReleaser.commandBuffer = {nullptr};
					newReleaser.signalSemaphore = { nullptr };
					found = queueFamilyToBarrierCollector.insert(castl::make_pair(queueFamily, newReleaser)).first;
				}
				return found->second;
			}
			void Release()
			{
				queueFamilyToBarrierCollector.clear();
			}
		};

		castl::shared_ptr<GPUGraph> m_Graph;
		//Rasterize Pass
		castl::vector<GPUPassInfo> m_Passes;
		//Transfer Pass
		castl::vector<GPUTransferInfo> m_TransferPasses;

		//Manager
		GraphExecutorImageManager m_ImageManager;
		GraphExecutorBufferManager m_BufferManager;
		FrameBoundResourcePool* m_FrameBoundResourceManager;

		//External Resource States
		ExternalResourceReleasingBarriers m_ExternalResourceReleasingBarriers;//Release External Resources From Their Last Queue To Where They Are Used
		castl::unordered_map<ImageHandle, ResourceState> m_ExternImageFinalUsageStates;
		castl::unordered_map<BufferHandle, ResourceState> m_ExternBufferFinalUsageStates;

		//Command Buffers
		castl::vector<vk::CommandBuffer> m_CommandBuffers;
		castl::vector<vk::PipelineStageFlags> m_CommandFinishStages;
		castl::vector<CommandBatchRange> m_CommandBufferBatchList;
		castl::vector<vk::Semaphore> m_LeafBatchSemaphores;
		castl::vector<vk::PipelineStageFlags> m_LeafBatchFinishStageFlags;
	};
}