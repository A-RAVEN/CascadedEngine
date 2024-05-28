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

	struct GPUPassInfo
	{
		castl::shared_ptr<FramebufferObject> m_FrameBufferObject;
		castl::shared_ptr<RenderPassObject> m_RenderPassObject;
		castl::vector<vk::ClearValue> m_ClearValues;
		castl::vector<GPUPassBatchInfo> m_Batches;
		VulkanBarrierCollector m_BarrierCollector;
	};

	struct GPUTransferInfo
	{
		VulkanBarrierCollector m_BarrierCollector;
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
			CA_ASSERT(found != m_HandleNameToResourceIndex.end(), "Buffer not found");
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
			CA_ASSERT(found != m_HandleNameToResourceIndex.end(), "Image not found");
			auto& subAllocator = m_SubAllocators[found->second.first];
			return subAllocator.m_Images[found->second.second];
		}
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
			, castl::unordered_map<vk::Buffer, castl::pair<ResourceUsageFlags, uint32_t>, cacore::hash<vk::Buffer>>& inoutBufferUsageFlagCache
			, DrawCallBatch const& batch
			, GPUPassBatchInfo const& batchInfo
			, uint32_t passID
		);

		void PrepareShaderArgsResourceBarriers(VulkanBarrierCollector& inoutBarrierCollector
			, castl::unordered_map<vk::Image, castl::pair<ResourceUsageFlags, uint32_t>, cacore::hash<vk::Image>>& inoutImageUsageFlagCache
			, castl::unordered_map<vk::Buffer, castl::pair<ResourceUsageFlags, uint32_t>, cacore::hash<vk::Buffer>>& inoutBufferUsageFlagCache
			, ShaderArgList const* shaderArgList
			, uint32_t passID
		);
		VulkanBarrierCollector& GetBarrierCollector(uint32_t passID);
		void PrepareFrameBufferAndPSOs();
		void PrepareResourceBarriers();
		void RecordGraph();

		GPUTextureDescriptor const* GetTextureHandleDescriptor(ImageHandle const& handle) const;
		vk::ImageView GetTextureHandleImageView(ImageHandle const& handle, GPUTextureView const& view) const;
		vk::Image GetTextureHandleImageObject(ImageHandle const& handle) const;
		vk::Buffer GetBufferHandleBufferObject(BufferHandle const& handle) const;

		virtual vk::Buffer GetBufferFromHandle(BufferHandle const& handle) override { return GetBufferHandleBufferObject(handle); }
		virtual vk::ImageView GetImageView(ImageHandle const& handle, GPUTextureView const& view) override { return GetTextureHandleImageView(handle, view);  }
	private:
		castl::shared_ptr<GPUGraph> m_Graph;
		//Rasterize Pass
		castl::vector<GPUPassInfo> m_Passes;
		//Transfer Pass
		castl::vector<GPUTransferInfo> m_TransferPasses;

		//Manager
		GraphExecutorImageManager m_ImageManager;
		GraphExecutorBufferManager m_BufferManager;
		FrameBoundResourcePool* m_FrameBoundResourceManager;

		//Command Buffers
		castl::vector<vk::CommandBuffer> m_GraphicsCommandBuffers;
		castl::vector<vk::Semaphore> m_WaitingSemaphores;
	};
}