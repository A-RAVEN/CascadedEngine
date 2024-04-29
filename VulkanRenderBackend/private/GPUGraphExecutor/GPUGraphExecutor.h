#pragma once
#include <GPUGraph.h>
#include <VulkanApplication.h>
#include <VulkanApplicationSubobjectBase.h>
#include <VulkanImageObject.h>
#include <InterfaceTranslator.h>
#include "ShaderBindingHolder.h"
namespace graphics_backend
{
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

		void AllocateResources(CVulkanApplication& app, ResManager const& bufferHandleManager)
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
				m_SubAllocators[descAllocatorPair.second].Allocate(app, *desc);
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
		void Allocate(CVulkanApplication& app, GPUBufferDescriptor const& descriptor)
		{
			m_Buffers.clear();
			m_Buffers.reserve(passAllocationCount);
			for (int i = 0; i < passAllocationCount; ++i)
			{
				auto bufferObj = app.GetMemoryManager().AllocateBuffer(EMemoryType::GPU
					, EMemoryLifetime::FrameBound
					, descriptor.count * descriptor.stride
					, EBufferUsageFlagsTranslate(descriptor.usageFlags));
				m_Buffers.push_back(castl::move(bufferObj));
			}
		}

		virtual void Release()
		{
			SubAllocator::Release();
			m_Buffers.clear();
		}

		castl::vector<VulkanBufferHandle> m_Buffers;
	};

	class GraphExecutorBufferManager : public GraphExecutorResourceManager<BufferSubAllocator, GraphResourceManager<GPUBufferDescriptor>>
	{
	public:
		VulkanBufferHandle const& GetBufferObject(castl::string const& handleName) const
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
		void Allocate(CVulkanApplication& app, GPUTextureDescriptor const& descriptor)
		{
			m_Images.clear();
			m_Images.reserve(passAllocationCount);
			for (int i = 0; i < passAllocationCount; ++i)
			{
				auto imgObj = app.GetMemoryManager().AllocateImage(descriptor, EMemoryType::GPU, EMemoryLifetime::FrameBound);
				m_Images.push_back(castl::move(imgObj));
				m_ImageViews.push_back(app.CreateDefaultImageView(descriptor, imgObj->GetImage(), true, true));
			}
		}

		virtual void Release()
		{
			SubAllocator::Release();
			m_Images.clear();
		}

		castl::vector<VulkanImageObject> m_Images;
		castl::vector<vk::ImageView> m_ImageViews;
	};

	class GraphExecutorImageManager : public GraphExecutorResourceManager<ImageSubAllocator, GraphResourceManager<GPUTextureDescriptor>>
	{
	public:
		VulkanImageObject const& GetImageObject(castl::string const& handleName) const
		{
			auto found = m_HandleNameToResourceIndex.find(handleName);
			CA_ASSERT(found != m_HandleNameToResourceIndex.end(), "Image not found");
			auto& subAllocator = m_SubAllocators[found->second.first];
			return subAllocator.m_Images[found->second.second];
		}

		vk::ImageView GetImageView(castl::string const& handleName) const
		{
			auto found = m_HandleNameToResourceIndex.find(handleName);
			CA_ASSERT(found != m_HandleNameToResourceIndex.end(), "Image not found");
			auto& subAllocator = m_SubAllocators[found->second.first];
			return subAllocator.m_ImageViews[found->second.second];
		}
	};

	class GPUGraphExecutor : public VKAppSubObjectBase, public ShadderResourceProvider
	{
	public:
		void PrepareGraph();
	private:
		void PrepareResources();

		void PrepareVertexBuffersBarriers(VulkanBarrierCollector& inoutBarrierCollector
			, castl::unordered_map<vk::Buffer, ResourceUsageFlags, cacore::hash<vk::Buffer>>& inoutBufferUsageFlagCache
			, DrawCallBatch const& batch
			, GPUPassBatchInfo const& batchInfo
		);

		void PrepareShaderArgsResourceBarriers(VulkanBarrierCollector& inoutBarrierCollector
			, castl::unordered_map<vk::Image, ResourceUsageFlags, cacore::hash<vk::Image>>& inoutImageUsageFlagCache
			, castl::unordered_map<vk::Buffer, ResourceUsageFlags, cacore::hash<vk::Buffer>>& inoutBufferUsageFlagCache
			, ShaderArgList const* shaderArgList);
		void PrepareFrameBufferAndPSOs();
		void RecordGraph();

		GPUTextureDescriptor const* GetTextureHandleDescriptor(ImageHandle const& handle) const;
		vk::ImageView GetTextureHandleImageView(ImageHandle const& handle) const;
		vk::Image GetTextureHandleImageObject(ImageHandle const& handle) const;
		vk::Buffer GetBufferHandleBufferObject(BufferHandle const& handle) const;

		virtual vk::Buffer GetBufferFromHandle(BufferHandle const& handle) override { return GetBufferHandleBufferObject(handle); }
		virtual vk::ImageView GetImageView(ImageHandle const& handle) override { return GetTextureHandleImageView(handle);  }
	private:
		GPUGraph m_Graph;
		//Runtime
		castl::vector<GPUPassInfo> m_Passes;
		//
		castl::vector<vk::CommandBuffer> m_GraphicsCommandBuffers;
		//Manager
		GraphExecutorImageManager m_ImageManager;
		GraphExecutorBufferManager m_BufferManager;
	};
}