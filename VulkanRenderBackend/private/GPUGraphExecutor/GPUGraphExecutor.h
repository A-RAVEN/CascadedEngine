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
		castl::vector<GPUPassBatchInfo> m_Batches;
		VulkanBarrierCollector m_BarrierCollector;
	};

	class GraphExecutorImageManager
	{
	public:
		struct ImageSubAllocator
		{
			uint32_t passAllocationCount;
			uint32_t totalCount;
			uint32_t AllocIndex()
			{
				++passAllocationCount;
				totalCount = castl::max(passAllocationCount, totalCount);
				return passAllocationCount - 1;
			}
			void ResetAllocationIndex()
			{
				passAllocationCount = 0;
			}

			void Reset()
			{
				passAllocationCount = 0;
				totalCount = 0;
			}

			void Release()
			{
				Reset();
				m_Images.clear();
			}

			void AllocateImages(CVulkanApplication& app, GPUTextureDescriptor const& descriptor)
			{
				m_Images.clear();
				m_Images.reserve(totalCount);
				for (int i = 0; i < totalCount; ++i)
				{
					auto imgObj = app.GetMemoryManager().AllocateImage(descriptor, EMemoryType::GPU, EMemoryLifetime::FrameBound);
					m_Images.push_back(castl::move(imgObj));
					m_ImageViews.push_back(app.CreateDefaultImageView(descriptor, imgObj->GetImage(), true, true));
				}
			}
			castl::vector<VulkanImageObject> m_Images;
			castl::vector<vk::ImageView> m_ImageViews;
		};

		void StateImageAllocation(castl::string const& handleName, int32_t descriptorIndex)
		{
			auto found = m_HandleNameToImageIndex.find(handleName);
			if (found == m_HandleNameToImageIndex.end())
			{
				uint32_t suballocatorID = GetSubAllocatorIndex(descriptorIndex);
				uint32_t allocIndex = m_ImageSubAllocators[suballocatorID].AllocIndex();
				m_HandleNameToImageIndex.insert(castl::make_pair(handleName, castl::make_pair(suballocatorID, allocIndex)));
			}
		}

		void ResetAllocationIndices()
		{
			for (auto& suAllocator : m_ImageSubAllocators)
			{
				suAllocator.ResetAllocationIndex();
			}
		}

		void AllocTextures(CVulkanApplication& app, GraphResourceManager<GPUTextureDescriptor> const& textureHandleManager)
		{
			for (auto& descAllocatorPair : m_DescriptorIndexToSubAllocator)
			{
				auto desc = textureHandleManager.DescriptorIDToDescriptor(descAllocatorPair.first);
				CA_ASSERT(desc != nullptr, "Descriptor not found");
				m_ImageSubAllocators[descAllocatorPair.second].AllocateImages(app, *desc);
			}
		}

		void ReleaseAll()
		{
			for (auto& allocator : m_ImageSubAllocators)
			{
				allocator.Release();
			}
			m_ImageSubAllocators.clear();
			m_DescriptorIndexToSubAllocator.clear();
			m_HandleNameToImageIndex.clear();
		}

		VulkanImageObject const& GetImageObject(castl::string const& handleName) const
		{
			auto found = m_HandleNameToImageIndex.find(handleName);
			CA_ASSERT(found != m_HandleNameToImageIndex.end(), "Image not found");
			auto& subAllocator = m_ImageSubAllocators[found->second.first];
			return subAllocator.m_Images[found->second.second];
		}
		
		vk::ImageView GetImageView(castl::string const& handleName) const
		{
			auto found = m_HandleNameToImageIndex.find(handleName);
			CA_ASSERT(found != m_HandleNameToImageIndex.end(), "Image not found");
			auto& subAllocator = m_ImageSubAllocators[found->second.first];
			return subAllocator.m_ImageViews[found->second.second];
		}
	private:
		uint32_t GetSubAllocatorIndex(int32_t descIndex)
		{
			auto found = m_DescriptorIndexToSubAllocator.find(descIndex);
			if (found == m_DescriptorIndexToSubAllocator.end())
			{
				found = m_DescriptorIndexToSubAllocator.insert(castl::make_pair(descIndex, m_ImageSubAllocators.size())).first;
				m_ImageSubAllocators.emplace_back();
			}
			return found->second;
		}

		castl::vector<ImageSubAllocator> m_ImageSubAllocators;
		castl::unordered_map<int32_t, uint32_t> m_DescriptorIndexToSubAllocator;
		castl::unordered_map<castl::string, castl::pair<uint32_t, uint32_t>> m_HandleNameToImageIndex;
	};

	class GraphExecutorBufferManager
	{
	public:
		struct SubAllocator
		{
			uint32_t passAllocationCount;
			uint32_t totalCount;
			uint32_t AllocIndex()
			{
				++passAllocationCount;
				totalCount = castl::max(passAllocationCount, totalCount);
				return passAllocationCount - 1;
			}
			void ResetAllocationIndex()
			{
				passAllocationCount = 0;
			}

			void Reset()
			{
				passAllocationCount = 0;
				totalCount = 0;
			}

			void Release()
			{
				Reset();
				m_Buffers.clear();
			}

			void AllocateBuffer(CVulkanApplication& app, GPUBufferDescriptor const& descriptor)
			{
				m_Buffers.clear();
				m_Buffers.reserve(totalCount);
				for (int i = 0; i < totalCount; ++i)
				{
					auto bufferObj = app.GetMemoryManager().AllocateBuffer(EMemoryType::GPU
						, EMemoryLifetime::FrameBound
						, descriptor.count * descriptor.stride
						, EBufferUsageFlagsTranslate(descriptor.usageFlags));
					m_Buffers.push_back(castl::move(bufferObj));
				}
			}
			castl::vector<VulkanBufferHandle> m_Buffers;
		};

		void AllocBufferIndex(castl::string const& handleName, int32_t descriptorIndex)
		{
			auto found = m_HandleNameToBufferIndex.find(handleName);
			if (found == m_HandleNameToBufferIndex.end())
			{
				uint32_t suballocatorID = GetSubAllocatorIndex(descriptorIndex);
				uint32_t allocIndex = m_SubAllocators[suballocatorID].AllocIndex();
				m_HandleNameToBufferIndex.insert(castl::make_pair(handleName, castl::make_pair(suballocatorID, allocIndex)));
			}
		}

		void ResetAllocationIndices()
		{
			for (auto& suAllocator : m_SubAllocators)
			{
				suAllocator.ResetAllocationIndex();
			}
		}

		void AllocBuffers(CVulkanApplication& app, GraphResourceManager<GPUBufferDescriptor> const& bufferHandleManager)
		{
			for (auto& descAllocatorPair : m_DescriptorIndexToSubAllocator)
			{
				auto desc = bufferHandleManager.DescriptorIDToDescriptor(descAllocatorPair.first);
				CA_ASSERT(desc != nullptr, "Descriptor not found");
				m_SubAllocators[descAllocatorPair.second].AllocateBuffer(app, *desc);
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
			m_HandleNameToBufferIndex.clear();
		}

		VulkanBufferHandle const& GetBufferObject(castl::string const& handleName) const
		{
			auto found = m_HandleNameToBufferIndex.find(handleName);
			CA_ASSERT(found != m_HandleNameToBufferIndex.end(), "Buffer not found");
			auto& subAllocator = m_SubAllocators[found->second.first];
			return subAllocator.m_Buffers[found->second.second];
		}


	private:
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

		castl::vector<SubAllocator> m_SubAllocators;
		castl::unordered_map<int32_t, uint32_t> m_DescriptorIndexToSubAllocator;
		castl::unordered_map<castl::string, castl::pair<uint32_t, uint32_t>> m_HandleNameToBufferIndex;
	};

	class GPUGraphExecutor : public VKAppSubObjectBase, public ShadderResourceProvider
	{
	public:
		void PrepareGraph();
	private:
		void PrepareResources();

		void PrepareShaderArgsImageBarriers(VulkanBarrierCollector& inoutBarrierCollector
			, castl::unordered_map<vk::Image, ResourceUsageFlags>& inoutImageUsageFlagCache
			, castl::unordered_map<vk::Buffer, ResourceUsageFlags>& inoutBufferUsageFlagCache
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