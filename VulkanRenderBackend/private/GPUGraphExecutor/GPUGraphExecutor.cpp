#include <pch.h>
#include "GPUGraphExecutor.h"
#include <VulkanApplication.h>
#include <GPUGraphExecutor/ShaderBindingHolder.h>
#include <InterfaceTranslator.h>
#include <CommandList_Impl.h>
#include <GPUResources/VKGPUBuffer.h>
#include <GPUResources/VKGPUTexture.h>

namespace graphics_backend
{
	//if source state and dst state have different queue family and source usage is not DontCare, a release barrier is required
	bool NeedReleaseBarrier(ResourceState const& srcState, ResourceState const& dstState)
	{
		return (srcState.queueFamily != dstState.queueFamily) && (srcState.usage != ResourceUsage::eDontCare);
	}

	ResourceState GetHandleInitializeUsage(BufferHandle const& handle, int currentPassID, PassInfoBase& currentPass)
	{
		switch (handle.GetType())
		{
			case BufferHandle::BufferType::Internal:
			{
				return ResourceState(currentPassID, ResourceUsage::eDontCare, currentPass.GetQueueFamily());
			}
			case BufferHandle::BufferType::External:
			{
				auto buffer = castl::static_shared_pointer_cast<VKGPUBuffer>(handle.GetExternalManagedBuffer());
				if (buffer->GetUsage() == ResourceUsage::eDontCare)
				{
					return ResourceState(currentPassID, ResourceUsage::eDontCare, currentPass.GetQueueFamily());
				}
				else
				{
					return ResourceState(-1, buffer->GetUsage(), buffer->GetQueueFamily());
				}
			}
		default:
			break;
		}
	}

	ResourceState GetHandleInitializeUsage(ImageHandle const& handle, int currentPassID, PassInfoBase& currentPass)
	{
		switch (handle.GetType())
		{
		case ImageHandle::ImageType::Internal:
		{
			return ResourceState(currentPassID, ResourceUsage::eDontCare, currentPass.GetQueueFamily());
		}
		case ImageHandle::ImageType::External:
		{
			auto image = castl::static_shared_pointer_cast<VKGPUTexture>(handle.GetExternalManagedTexture());
			if (image->GetUsage() == eDontCare)
			{
				return ResourceState(currentPassID, ResourceUsage::eDontCare, currentPass.GetQueueFamily());
			}
			else
			{
				return ResourceState(-1, image->GetUsage(), image->GetQueueFamily());
			}
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			//TODO: 可能会有不丢弃backbuffer内容的需求
			return ResourceState(currentPassID, ResourceUsage::eDontCare, currentPass.GetQueueFamily());
		}
		default:
			break;
		}
	}

	ResourceState MakeNewResourceState(int passID, uint32_t queueFamilyIndex, ResourceUsageFlags usage)
	{
		return ResourceState(passID, usage, queueFamilyIndex);
	}

	//TODO: Same Sematics May be found in multiple attribute data
	CVertexInputDescriptor MakeVertexInputDescriptors(castl::map<castl::string, VertexInputsDescriptor> const& vertexAttributeDescs
		, castl::vector<ShaderCompilerSlang::ShaderVertexAttributeData> const& vertexAttributes
		, InputAssemblyStates assemblyStates
		, castl::map<castl::string, BufferHandle> const& boundVertexBuffers
		, castl::unordered_map<castl::string, VertexAttributeBindingData>& inoutBindingNameToIndex)
	{
		CVertexInputDescriptor result;
		result.assemblyStates = assemblyStates;

		//result.m_PrimitiveDescriptions.resize(vertexAttributes.size());

		for (auto& attributeData : vertexAttributes)
		{
			bool attribBindingFound = false;
			for (auto boundPair : boundVertexBuffers)
			{
				auto& boundAttribDescName = boundPair.first;
				auto foundAttribDesc = vertexAttributeDescs.find(boundAttribDescName);
				CA_ASSERT(foundAttribDesc != vertexAttributeDescs.end(), "Attribute Descriptor Not Found");
				for (auto& attribute : foundAttribDesc->second.attributes)
				{
					if ((attribute.semanticName == attributeData.m_SematicName) && (attribute.sematicIndex == attributeData.m_SematicIndex))
					{
						auto found = inoutBindingNameToIndex.find(boundAttribDescName);
						if (found == inoutBindingNameToIndex.end())
						{
							uint32_t bindingID = inoutBindingNameToIndex.size();
							found = inoutBindingNameToIndex.insert(castl::make_pair(boundAttribDescName, VertexAttributeBindingData{ bindingID , foundAttribDesc->second.stride, {}, foundAttribDesc->second.perInstance })).first;
						}
						else
						{
						}
						found->second.attributes.push_back(VertexAttribute{ attributeData.m_Location , attribute.offset, attribute.format });
						attribBindingFound = true;
						break;
					}
				}
				if (attribBindingFound)
				{
					break;
				}
			}
			CA_ASSERT(attribBindingFound, "Attribute With Semantic" + attributeData.m_SematicName + "Not Found");
		}
		for (auto pair : inoutBindingNameToIndex)
		{
			if (result.m_PrimitiveDescriptions.size() <= pair.second.bindingIndex)
			{
				result.m_PrimitiveDescriptions.resize(pair.second.bindingIndex + 1);
			}
			result.m_PrimitiveDescriptions[pair.second.bindingIndex] = castl::make_tuple(pair.second.stride, pair.second.attributes, pair.second.bInstance);
		}

		return result;
	}

	void GPUGraphExecutor::PrepareGraph()
	{
		//Alloc Image & Buffer Resources
		PrepareResources();
		//Prepare PSO & FrameBuffer & RenderPass
		PrepareFrameBufferAndPSOs();
		//Prepare Resource Barriers
		PrepareResourceBarriers();
		//Record CommandBuffers
		RecordGraph();
		//Scan Command Batches
		ScanCommandBatchs();
		//Submit
		Submit();
		//Sync Final Usages
		SyncExternalResources();
	}


	void GPUGraphExecutor::PrepareResources()
	{
		auto& imageManager = m_Graph->GetImageManager();
		auto& bufferManager = m_Graph->GetBufferManager();
		auto renderPasses = m_Graph->GetRenderPasses();
		m_BufferManager.ResetAllocator();
		m_ImageManager.ResetAllocator();
		for (auto& renderPass : renderPasses)
		{
			//Rendertargets
			auto& imageHandles = renderPass.GetAttachments();
			for (auto& img : imageHandles)
			{
				if (img.GetType() == ImageHandle::ImageType::Internal)
				{
					m_ImageManager.AllocResourceIndex(img.GetName(), imageManager.GetDescriptorIndex(img.GetName()));
				}
				else if (img.GetType() == ImageHandle::ImageType::Backbuffer)
				{
					castl::shared_ptr<CWindowContext> window = castl::static_shared_pointer_cast<CWindowContext>(img.GetWindowHandle());
					window->WaitCurrentFrameBufferIndex();
				}
			}
			//Drawcalls
			auto& drawcallBatchs = renderPass.GetDrawCallBatches();
			castl::deque<ShaderArgList const*> shaderArgLists;
			for (auto& batch : drawcallBatchs)
			{
				auto& shaderArgs = batch.shaderArgs;
				if (batch.shaderArgs != nullptr)
				{
					shaderArgLists.push_back(shaderArgs.get());
				}

				{
					//Index Buffer
					auto& indesBuffer = batch.m_BoundIndexBuffer;
					if (indesBuffer.GetType() == BufferHandle::BufferType::Internal)
					{
						m_BufferManager.AllocResourceIndex(indesBuffer.GetName(), bufferManager.GetDescriptorIndex(indesBuffer.GetName()));
					}
					//Vertex Buffers
					for(auto& vertexBufferPair : batch.m_BoundVertexBuffers)
					{
						auto& vertexBuffer = vertexBufferPair.second;
						if (vertexBuffer.GetType() == BufferHandle::BufferType::Internal)
						{
							m_BufferManager.AllocResourceIndex(vertexBuffer.GetName(), bufferManager.GetDescriptorIndex(vertexBuffer.GetName()));
						}
					}
				}

			}
			while (!shaderArgLists.empty())
			{
				ShaderArgList const& shaderArgs = *shaderArgLists.front();
				shaderArgLists.pop_front();
				for (auto& subArgPairs: shaderArgs.GetSubArgList())
				{
					shaderArgLists.push_back(subArgPairs.second.get());
				}
				for (auto& imagePair : shaderArgs.GetImageList())
				{
					auto& imgs = imagePair.second;
					for (auto& img : imgs)
					{
						auto& imgHandle = img.first;
						if (imgHandle.GetType() == ImageHandle::ImageType::Internal)
						{
							m_ImageManager.AllocResourceIndex(imgHandle.GetName(), imageManager.GetDescriptorIndex(imgHandle.GetName()));
						}
						else if (imgHandle.GetType() == ImageHandle::ImageType::Backbuffer)
						{
							castl::shared_ptr<CWindowContext> window = castl::static_shared_pointer_cast<CWindowContext>(imgHandle.GetWindowHandle());
							window->WaitCurrentFrameBufferIndex();
						}
					}
				}
				for (auto& bufferPair : shaderArgs.GetBufferList())
				{
					auto& bufs = bufferPair.second;
					for (auto& buf : bufs)
					{
						if (buf.GetType() == BufferHandle::BufferType::Internal)
						{
							m_BufferManager.AllocResourceIndex(buf.GetName(), bufferManager.GetDescriptorIndex(buf.GetName()));
						}
					}
				}
			}

			m_ImageManager.NextPass();
			m_BufferManager.NextPass();
		}

		m_ImageManager.AllocateResources(GetVulkanApplication(), m_FrameBoundResourceManager, m_Graph->GetImageManager());
		m_BufferManager.AllocateResources(GetVulkanApplication(), m_FrameBoundResourceManager, m_Graph->GetBufferManager());
	}

	GPUTextureDescriptor const* GPUGraphExecutor::GetTextureHandleDescriptor(ImageHandle const& handle) const
	{
		auto imageType = handle.GetType();
		switch (imageType)
		{
			case ImageHandle::ImageType::Internal:
			{
				return m_Graph->GetImageManager().GetDescriptor(handle.GetName());
			}
			case ImageHandle::ImageType::External:
			{
				return &handle.GetExternalManagedTexture()->GetDescriptor();
			}
			case ImageHandle::ImageType::Backbuffer:
			{
				return &handle.GetWindowHandle()->GetBackbufferDescriptor();
			}
		}
		return nullptr;
	}

	PassInfoBase* GPUGraphExecutor::GetBasePassInfo(int passID)
	{
		if (passID >= 0)
		{
			auto& graphStages = m_Graph->GetGraphStages();
			auto& passIndices = m_Graph->GetPassIndices();
			auto stage = graphStages[passID];
			uint32_t realPassIndex = passIndices[passID];
			switch (stage)
			{
			case GPUGraph::EGraphStageType::eRenderPass:
				return &m_Passes[realPassIndex];
			case GPUGraph::EGraphStageType::eTransferPass:
				return &m_TransferPasses[realPassIndex];
			}
		}
		return nullptr;
	}

	vk::ImageView GPUGraphExecutor::GetTextureHandleImageView(ImageHandle const& handle, GPUTextureView const& view) const
	{
		auto& imageManager = m_Graph->GetImageManager();
		auto imageType = handle.GetType();
		switch (imageType)
		{
		case ImageHandle::ImageType::Internal:
		{
			auto& image = m_ImageManager.GetImageObject(handle.GetName());
			auto& desc = *imageManager.GetDescriptor(handle.GetName());
			return m_FrameBoundResourceManager->resourceObjectManager.EnsureImageView(image.image, desc, view);
		}
		case ImageHandle::ImageType::External:
		{
			castl::shared_ptr<VKGPUTexture> texture = castl::static_shared_pointer_cast<VKGPUTexture>(handle.GetExternalManagedTexture());
			return texture->EnsureImageView(view);
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			castl::shared_ptr<CWindowContext> window = castl::static_shared_pointer_cast<CWindowContext>(handle.GetWindowHandle());
			return window->EnsureCurrentFrameImageView(view);
		}
		}
		CA_LOG_ERR("Invalid Image Handle For Getting Image View");
		return {};
	}

	vk::Image GPUGraphExecutor::GetTextureHandleImageObject(ImageHandle const& handle) const
	{
		auto imageType = handle.GetType();
		switch (imageType)
		{
		case ImageHandle::ImageType::Internal:
		{
			return m_ImageManager.GetImageObject(handle.GetName()).image;
		}
		case ImageHandle::ImageType::External:
		{
			castl::shared_ptr<VKGPUTexture> texture = castl::static_shared_pointer_cast<VKGPUTexture>(handle.GetExternalManagedTexture());
			return texture->GetImage().image;
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			castl::shared_ptr<CWindowContext> window = castl::static_shared_pointer_cast<CWindowContext>(handle.GetWindowHandle());
			return window->GetCurrentFrameImage();
		}
		}
		CA_LOG_ERR("Invalid Image Handle For Getting Image");
		return {};
	}

	vk::Buffer GPUGraphExecutor::GetBufferHandleBufferObject(BufferHandle const& handle) const
	{
		auto bufferType = handle.GetType();
		switch (bufferType)
		{
		case BufferHandle::BufferType::Internal:
		{
			return m_BufferManager.GetBufferObject(handle.GetName()).buffer;
		}
		case BufferHandle::BufferType::External:
		{
			castl::shared_ptr<VKGPUBuffer> buffer = castl::static_shared_pointer_cast<VKGPUBuffer>(handle.GetExternalManagedBuffer());
			return buffer->GetBuffer().buffer;
		}
		}
		return {};
	}

	void GPUGraphExecutor::ScanCommandBatchs()
	{

		for (auto& pair : m_ExternalResourceReleasingBarriers.queueFamilyToBarrierCollector)
		{
			uint32_t queueFamilyIndex = pair.first;
			auto& releaser = pair.second;
			releaser.signalSemaphore = m_FrameBoundResourceManager->semaphorePool.AllocSemaphore();
		}

		auto& graphStages = m_Graph->GetGraphStages();
		castl::vector<uint32_t> passToBatchID;
		passToBatchID.resize(graphStages.size());
		m_CommandBufferBatchList.clear();
		m_CommandBufferBatchList.push_back(CommandBatchRange::Create(GetBasePassInfo(0)->m_BarrierCollector.GetQueueFamily(), 0));
		auto lastBatch = &m_CommandBufferBatchList.back();
		for (uint32_t passID = 0; passID < graphStages.size(); ++passID)
		{
			auto pass = GetBasePassInfo(passID);
			uint32_t startCommandID = m_FinalCommandBuffers.size();
			for (vk::CommandBuffer cmd : pass->m_CommandBuffers)
			{
				m_FinalCommandBuffers.push_back(cmd);
			}
			uint32_t lastCommandID = m_FinalCommandBuffers.size() - 1;
			uint32_t queueFamilyID = pass->m_BarrierCollector.GetQueueFamily();
			if (lastBatch->queueFamilyIndex != queueFamilyID)
			{
				m_CommandBufferBatchList.push_back(CommandBatchRange::Create(pass->m_BarrierCollector.GetQueueFamily(), startCommandID));
				lastBatch = &m_CommandBufferBatchList.back();
			}
			lastBatch->lastCommand = castl::max(lastBatch->lastCommand, lastCommandID);

			lastBatch->hasSuccessor = lastBatch->hasSuccessor || (pass->m_SuccessorPasses.size() > 0);
			for (uint32_t predPassID : pass->m_PredecessorPasses)
			{
				lastBatch->waitingBatch.insert(passToBatchID[predPassID]);
			}
			for (uint32_t queueReleaserID : pass->m_WaitingQueueFamilies)
			{
				lastBatch->waitingQueueFamilyReleaser.insert(queueReleaserID);
			}
			passToBatchID[passID] = m_CommandBufferBatchList.size() - 1;
		}

		//m_LeafBatchSemaphores.clear();
		//m_LeafBatchFinishStageFlags.clear();
		for (auto& batch : m_CommandBufferBatchList)
		{
			batch.signalSemaphore = m_FrameBoundResourceManager->semaphorePool.AllocSemaphore();
			if (!batch.hasSuccessor)
			{
				m_FrameBoundResourceManager->AddLeafSempahores(batch.signalSemaphore);
				//m_LeafBatchSemaphores.push_back(batch.signalSemaphore);
				//m_LeafBatchFinishStageFlags.push_back(vk::PipelineStageFlagBits::eAllCommands);
			}
			batch.waitSemaphores.reserve(batch.waitingBatch.size());
			for (uint32_t waitingBatchID : batch.waitingBatch)
			{
				batch.waitSemaphores.push_back(m_CommandBufferBatchList[waitingBatchID].signalSemaphore);
				batch.waitStages.push_back(vk::PipelineStageFlagBits::eAllCommands);
			}
			for (uint32_t queueFamilyReleaserID : batch.waitingQueueFamilyReleaser)
			{
				auto& releaser = m_ExternalResourceReleasingBarriers.queueFamilyToBarrierCollector[queueFamilyReleaserID];
				batch.waitSemaphores.push_back(releaser.signalSemaphore);
				batch.waitStages.push_back(vk::PipelineStageFlagBits::eAllCommands);
			}
		}
	}

	void GPUGraphExecutor::Submit()
	{
		for (auto& pair : m_ExternalResourceReleasingBarriers.queueFamilyToBarrierCollector)
		{
			uint32_t queueFamilyIndex = pair.first;
			auto& releaser = pair.second;
			GetQueueContext().SubmitCommands(queueFamilyIndex
				, 0
				, releaser.commandBuffer
				, {}
				, {}
				, {}
				, releaser.signalSemaphore);
		}
		for (auto& batch : m_CommandBufferBatchList)
		{
			vk::CommandBuffer* pCommand = &m_FinalCommandBuffers[batch.firstCommand];
			uint32_t count = batch.lastCommand - batch.firstCommand + 1;
			vk::ArrayProxyNoTemporaries<vk::CommandBuffer> proxy = { count, pCommand };
			GetQueueContext().SubmitCommands(batch.queueFamilyIndex
				, 0
				, proxy
				, {}
				, batch.waitSemaphores
				, batch.waitStages
				, batch.signalSemaphore);
		}

		//GetQueueContext()
		//	.SubmitCommands(GetQueueContext().GetGraphicsQueueFamily()
		//		, 0
		//		, {}
		//		, m_FrameBoundResourceManager->GetFence()
		//		, m_LeafBatchSemaphores
		//		, m_LeafBatchFinishStageFlags);
	}

	void GPUGraphExecutor::SyncExternalResources()
	{
		for (auto pair : m_ExternBufferFinalUsageStates)
		{
			auto& bufferHandle = pair.first;
			auto& finalUsage = pair.second;
			auto buffer = castl::static_pointer_cast<VKGPUBuffer>(bufferHandle.GetExternalManagedBuffer());
			buffer->SetUsage(finalUsage.usage);
			buffer->SetQueueFamily(finalUsage.queueFamily);
		}

		for (auto pair : m_ExternImageFinalUsageStates)
		{
			auto& imageHandle = pair.first;
			auto& finalUsage = pair.second;
			switch (imageHandle.GetType())
			{
			case ImageHandle::ImageType::External:
				{
					auto image = castl::static_pointer_cast<VKGPUTexture>(imageHandle.GetExternalManagedTexture());
					image->SetUsage(finalUsage.usage);
					image->SetQueueFamily(finalUsage.queueFamily);
				}
				break;
			case ImageHandle::ImageType::Backbuffer:
				{
					auto window = castl::static_pointer_cast<CWindowContext>(imageHandle.GetWindowHandle());
					window->GetSwapchainContext().MarkUsages(finalUsage.usage, finalUsage.queueFamily);
				}
				break;
			}
		}
	}

	void GPUGraphExecutor::UpdateExternalBufferUsage(PassInfoBase* passInfo, BufferHandle const& handle, ResourceState const& initUsageState, ResourceState const& newUsageState)
	{
		if (handle.GetType() == BufferHandle::BufferType::External)
		{
			auto found = m_ExternBufferFinalUsageStates.find(handle);
			if (found == m_ExternBufferFinalUsageStates.end())
			{
				//Different Queue Needs Release barrier
				if (NeedReleaseBarrier(initUsageState, newUsageState))
				{
					auto& releaser = m_ExternalResourceReleasingBarriers.GetQueueFamilyReleaser(GetVulkanApplication(), initUsageState.queueFamily);
					releaser.barrierCollector.PushBufferReleaseBarrier(newUsageState.queueFamily, GetBufferHandleBufferObject(handle), initUsageState.usage, newUsageState.usage);
					passInfo->m_WaitingQueueFamilies.insert(initUsageState.queueFamily);
				}
			}
			m_ExternBufferFinalUsageStates[handle] = newUsageState;
		}
	}

	void GPUGraphExecutor::UpdateExternalImageUsage(PassInfoBase* passInfo, ImageHandle const& handle, ResourceState const& initUsageState, ResourceState const& newUsageState)
	{
		switch (handle.GetType())
		{
			case ImageHandle::ImageType::External:
			case ImageHandle::ImageType::Backbuffer:
			{
				auto found = m_ExternImageFinalUsageStates.find(handle);
				if (found == m_ExternImageFinalUsageStates.end())
				{
					//Different Queue Needs Release barrier
					if (NeedReleaseBarrier(initUsageState, newUsageState))
					{
						auto& releaser = m_ExternalResourceReleasingBarriers.GetQueueFamilyReleaser(GetVulkanApplication(), initUsageState.queueFamily);
						auto pDesc = GetTextureHandleDescriptor(handle);
						releaser.barrierCollector.PushImageReleaseBarrier(newUsageState.queueFamily, GetTextureHandleImageObject(handle), pDesc->format, initUsageState.usage, newUsageState.usage);
						passInfo->m_WaitingQueueFamilies.insert(initUsageState.queueFamily);
					}
				}
				m_ExternImageFinalUsageStates[handle] = newUsageState;
				break;
			}
		}
	}

	template<typename T>
	void UpdateResourceUsageFlags(castl::unordered_map<T, ResourceState>& inoutResourceUsageFlagCache
		, T resource, ResourceState const& resourceState)
	{
		inoutResourceUsageFlagCache[resource] = resourceState;
	};

	template<typename T>
	ResourceState GetResourceUsage(castl::unordered_map<T, ResourceState>& inoutResourceUsageFlagCache
		, T resource
		, ResourceState const defaultState)
	{
		auto found = inoutResourceUsageFlagCache.find(resource);
		if (found != inoutResourceUsageFlagCache.end())
		{
			return found->second;
		}
		return defaultState;
	};

	void GPUGraphExecutor::PrepareVertexBuffersBarriers(VulkanBarrierCollector& inoutBarrierCollector
		, castl::unordered_map<vk::Buffer, ResourceState>& inoutBufferUsageFlagCache
		, DrawCallBatch const& batch
		, GPUPassBatchInfo const& batchInfo
		, uint32_t passID
	)
	{
		if (batch.m_BoundIndexBuffer.GetType() != BufferHandle::BufferType::Invalid)
		{
			ResourceUsageFlags usageFlags = ResourceUsage::eVertexAttribute;
			UpdateBufferDependency(passID, batch.m_BoundIndexBuffer, usageFlags, inoutBufferUsageFlagCache);
		}
		for (auto bindingPair : batchInfo.m_VertexAttributeBindings)
		{
			auto foundBuffer = batch.m_BoundVertexBuffers.find(bindingPair.first);
			if (foundBuffer != batch.m_BoundVertexBuffers.end())
			{
				ResourceUsageFlags usageFlags = ResourceUsage::eVertexAttribute;
				UpdateBufferDependency(passID, foundBuffer->second, usageFlags, inoutBufferUsageFlagCache);
			}
		}
	}

	VulkanBarrierCollector& GPUGraphExecutor::GetBarrierCollector(uint32_t passID)
	{
		auto& graphStages = m_Graph->GetGraphStages();
		auto& passIndices = m_Graph->GetPassIndices();
		auto stage = graphStages[passID];
		uint32_t realPassIndex = passIndices[passID];
		switch (stage)
		{
		case GPUGraph::EGraphStageType::eRenderPass:
			return m_Passes[realPassIndex].m_BarrierCollector;
		case GPUGraph::EGraphStageType::eTransferPass:
			return m_TransferPasses[realPassIndex].m_BarrierCollector;
		}
	}

	void GPUGraphExecutor::UpdateBufferDependency(
		uint32_t destPassID
		, BufferHandle const& bufferHandle
		, ResourceUsageFlags newUsageFlags
		, castl::unordered_map<vk::Buffer, ResourceState>& inoutBufferUsageFlagCache)
	{
		if (bufferHandle.GetType() == BufferHandle::BufferType::Invalid)
		{
			return;
		}
		auto buffer = GetBufferHandleBufferObject(bufferHandle);
		if (buffer == vk::Buffer{nullptr})
			return;

		auto dstInfo = GetBasePassInfo(destPassID);
		CA_ASSERT(dstInfo != nullptr, "Invalid Dest Pass ID");

		auto usageStates = GetResourceUsage(inoutBufferUsageFlagCache, buffer, GetHandleInitializeUsage(bufferHandle, destPassID, *dstInfo));
		auto newUsageState = MakeNewResourceState(destPassID, dstInfo->GetQueueFamily(), newUsageFlags);
		CA_ASSERT(newUsageState.usage != ResourceUsage::eDontCare, "why dst usage is dont care?");
		if (usageStates.usage != newUsageState.usage)
		{
			auto sourceInfo = GetBasePassInfo(usageStates.passID);
			if (sourceInfo != nullptr)
			{
				if (NeedReleaseBarrier(usageStates, newUsageState))
				{
					sourceInfo->m_BarrierCollector.PushBufferReleaseBarrier(newUsageState.queueFamily, buffer, usageStates.usage, newUsageState.usage);
					sourceInfo->m_SuccessorPasses.insert(newUsageState.passID);
					dstInfo->m_PredecessorPasses.insert(usageStates.passID);
				}
			}
			dstInfo->m_BarrierCollector.PushBufferAquireBarrier(usageStates.queueFamily, buffer, usageStates.usage, newUsageState.usage);
			UpdateResourceUsageFlags(inoutBufferUsageFlagCache, buffer, newUsageState);
			UpdateExternalBufferUsage(dstInfo, bufferHandle, usageStates, newUsageState);
		}
	}

	void GPUGraphExecutor::UpdateImageDependency(uint32_t destPassID, ImageHandle const& imageHandle
		, ResourceUsageFlags newUsageFlags
		, castl::unordered_map<vk::Image, ResourceState>& inoutImageUsageFlagCache)
	{
		if (imageHandle.GetType() == ImageHandle::ImageType::Invalid)
		{
			return;
		}
		auto image = GetTextureHandleImageObject(imageHandle);
		if (image == vk::Image{nullptr})
			return;

		auto dstInfo = GetBasePassInfo(destPassID);
		CA_ASSERT(dstInfo != nullptr, "Invalid Dest Pass ID");

		auto pDesc = GetTextureHandleDescriptor(imageHandle);

		auto usageStates = GetResourceUsage(inoutImageUsageFlagCache, image, GetHandleInitializeUsage(imageHandle, destPassID, *dstInfo));
		auto newUsageState = MakeNewResourceState(destPassID, dstInfo->GetQueueFamily(), newUsageFlags);

		if (usageStates.usage != newUsageState.usage)
		{
			auto sourceInfo = GetBasePassInfo(usageStates.passID);
			if (sourceInfo != nullptr)
			{
				if (NeedReleaseBarrier(usageStates, newUsageState))
				{
					sourceInfo->m_BarrierCollector.PushImageReleaseBarrier(newUsageState.queueFamily, image, pDesc->format, usageStates.usage, newUsageState.usage);
					sourceInfo->m_SuccessorPasses.insert(newUsageState.passID);
					dstInfo->m_PredecessorPasses.insert(usageStates.passID);
				}
			}
			dstInfo->m_BarrierCollector.PushImageAquireBarrier(usageStates.queueFamily, image, pDesc->format, usageStates.usage, newUsageState.usage);
			UpdateResourceUsageFlags(inoutImageUsageFlagCache, image, newUsageState);
			UpdateExternalImageUsage(dstInfo, imageHandle, usageStates, newUsageState);
		}
	}

	GPUGraphExecutor::GPUGraphExecutor(CVulkanApplication& application) : VKAppSubObjectBaseNoCopy(application)
	{
	}

	void GPUGraphExecutor::Initialize(castl::shared_ptr<GPUGraph> const& gpuGraph, FrameBoundResourcePool* frameBoundResourceManager)
	{
		m_Graph = gpuGraph;
		m_FrameBoundResourceManager = frameBoundResourceManager;
	}

	void GPUGraphExecutor::Release()
	{
		m_Graph.reset();
		//Rasterize Passes
		m_Passes.clear();
		//Transfer Passes
		m_TransferPasses.clear();
		//Manager
		m_ImageManager.ReleaseAll();
		m_BufferManager.ReleaseAll();

		//Final Handle State
		m_ExternBufferFinalUsageStates.clear();
		m_ExternImageFinalUsageStates.clear();
		m_ExternalResourceReleasingBarriers.Release();
		//Command Buffers
		m_FinalCommandBuffers.clear();
		m_CommandBufferBatchList.clear();
		//m_LeafBatchSemaphores.clear();
		//m_LeafBatchFinishStageFlags.clear();
	}

	void GPUGraphExecutor::PrepareShaderArgsResourceBarriers(VulkanBarrierCollector& inoutBarrierCollector
		, castl::unordered_map<vk::Image, ResourceState>& inoutResourceUsageFlagCache
		, castl::unordered_map<vk::Buffer, ResourceState>& inoutBufferUsageFlagCache
		, ShaderArgList const* shaderArgList
		, uint32_t passID)
	{
		castl::deque<ShaderArgList const*> shaderArgLists = { shaderArgList };
		while (!shaderArgLists.empty())
		{
			ShaderArgList const& shaderArgs = *shaderArgLists.front();
			shaderArgLists.pop_front();
			for (auto& subArgPairs : shaderArgs.GetSubArgList())
			{
				shaderArgLists.push_back(subArgPairs.second.get());
			}
			for (auto& imagePair : shaderArgs.GetImageList())
			{
				auto& imgs = imagePair.second;
				for (auto& img : imgs)
				{
					auto& imgHandle = img.first;
					ResourceUsageFlags usageFlags = ResourceUsage::eVertexRead | ResourceUsage::eFragmentRead;
					UpdateImageDependency(passID, imgHandle, usageFlags, inoutResourceUsageFlagCache);
				}
			}
			for (auto& bufferPair : shaderArgs.GetBufferList())
			{
				auto& bufs = bufferPair.second;
				for (auto& buf : bufs)
				{
					ResourceUsageFlags usageFlags = ResourceUsage::eVertexRead | ResourceUsage::eFragmentRead;
					UpdateBufferDependency(passID, buf, usageFlags, inoutBufferUsageFlagCache);
				}
			}
		}
	}

	void GPUGraphExecutor::PrepareFrameBufferAndPSOs()
	{
		auto& renderPasses = m_Graph->GetRenderPasses();

		castl::unordered_map<vk::Image, ResourceUsageFlags, cacore::hash<vk::Image>> imageUsageFlagCache;
		castl::unordered_map<vk::Buffer, ResourceUsageFlags, cacore::hash<vk::Buffer>> bufferUsageFlagCache;

		for (auto& renderPass : renderPasses)
		{
			auto& attachments = renderPass.GetAttachments();
			auto& drawcallBatchs = renderPass.GetDrawCallBatches();
			GPUPassInfo passInfo{};
			//RenderPass Object
			{
				passInfo.m_ClearValues.resize(attachments.size());
				RenderPassDescriptor renderPassDesc{};
				renderPassDesc.renderPassInfo.attachmentInfos.resize(attachments.size());
				renderPassDesc.renderPassInfo.subpassInfos.resize(1);

				for (size_t i = 0; i < attachments.size(); ++i)
				{
					auto& attachment = attachments[i];
					auto& attachmentInfo = renderPassDesc.renderPassInfo.attachmentInfos[i];
					auto pDesc = GetTextureHandleDescriptor(attachment);
					attachmentInfo.format = pDesc->format;
					attachmentInfo.multiSampleCount = pDesc->samples;
					//TODO DO A Attachment Wise Version
					attachmentInfo.loadOp = renderPass.GetAttachmentLoadOp();
					attachmentInfo.storeOp = renderPass.GetAttachmentStoreOp();
					attachmentInfo.stencilLoadOp = renderPass.GetAttachmentLoadOp();
					attachmentInfo.stencilStoreOp = renderPass.GetAttachmentStoreOp();
					//attachmentInfo.clearValue = renderPass.GetClearValue();
					passInfo.m_ClearValues[i] = AttachmentClearValueTranslate(
						renderPass.GetClearValue()
						, pDesc->format);
				}

				{
					auto& subpass = renderPassDesc.renderPassInfo.subpassInfos[0];
					subpass.colorAttachmentIDs.reserve(attachments.size());
					for (uint32_t attachmentID = 0; attachmentID < attachments.size(); ++attachmentID)
					{
						if(attachmentID != renderPass.GetDepthAttachmentIndex())
						{
							subpass.colorAttachmentIDs.push_back(attachmentID);
						}
					}
					subpass.depthAttachmentID = renderPass.GetDepthAttachmentIndex();
				}
				passInfo.m_RenderPassObject = GetGPUObjectManager().GetRenderPassCache().GetOrCreate(renderPassDesc);
			}

			//Frame Buffer Object
			{
				auto attachmentType = attachments[0].GetType();

				FramebufferDescriptor frameBufferDesc{};
				auto pDesc = GetTextureHandleDescriptor(attachments[0]);
				CA_ASSERT(pDesc != nullptr, "Invalid texture descriptor");
				frameBufferDesc.height = pDesc->height;
				frameBufferDesc.width = pDesc->width;
				frameBufferDesc.layers = 1;
				frameBufferDesc.renderImageViews.resize(attachments.size());

				for(size_t i = 0; i < attachments.size(); ++i)
				{
					auto& attachment = attachments[i];
					auto pDesc = GetTextureHandleDescriptor(attachment);
					CA_ASSERT(pDesc != nullptr, "Invalid texture descriptor");
					frameBufferDesc.renderImageViews[i] = GetTextureHandleImageView(attachment, GPUTextureView::CreateDefaultForRenderTarget(pDesc->format));
					frameBufferDesc.renderpassObject = passInfo.m_RenderPassObject;
				}

				passInfo.m_FrameBufferObject = GetGPUObjectManager().GetFramebufferCache().GetOrCreate(frameBufferDesc);
			}

			//Batch Info
			for (auto& batch : drawcallBatchs)
			{
				GPUPassBatchInfo newBatchInfo{};

				auto& psoDesc = batch.pipelineStateDesc;
				auto vertShader = GetGPUObjectManager().m_ShaderModuleCache.GetOrCreate(psoDesc.m_ShaderSet->GetShaderSourceInfo(ShaderCompilerSlang::EShaderTargetType::eSpirV, ECompileShaderType::eVert));
				auto fragShader = GetGPUObjectManager().m_ShaderModuleCache.GetOrCreate(psoDesc.m_ShaderSet->GetShaderSourceInfo(ShaderCompilerSlang::EShaderTargetType::eSpirV, ECompileShaderType::eFrag));

				//Shader Binding Holder
				//Dont Need To Make Instance here, We Only Need Descriptor Set Layouts
				newBatchInfo.m_ShaderBindingInstance.InitShaderBindings(GetVulkanApplication(), m_FrameBoundResourceManager, psoDesc.m_ShaderSet->GetShaderReflectionData(ShaderCompilerSlang::EShaderTargetType::eSpirV));

				auto& vertexInputBindings = psoDesc.m_VertexInputBindings;
				auto& vertexAttributes = psoDesc.m_ShaderSet->GetShaderReflectionData(ShaderCompilerSlang::EShaderTargetType::eSpirV).m_VertexAttributes;
				CVertexInputDescriptor vertexInputDesc = MakeVertexInputDescriptors(vertexInputBindings
					, vertexAttributes
					, psoDesc.m_InputAssemblyStates
					, batch.m_BoundVertexBuffers
					, newBatchInfo.m_VertexAttributeBindings);

				CPipelineObjectDescriptor psoDescObj;
				psoDescObj.vertexInputs = vertexInputDesc;
				psoDescObj.pso = psoDesc.m_PipelineStates;
				psoDescObj.shaderState = { vertShader, fragShader };
				psoDescObj.renderPassObject = passInfo.m_RenderPassObject;
				psoDescObj.descriptorSetLayouts = newBatchInfo.m_ShaderBindingInstance.m_DescriptorSetsLayouts;

				newBatchInfo.m_PSO = GetGPUObjectManager().GetPipelineCache().GetOrCreate(psoDescObj);

				passInfo.m_Batches.push_back(newBatchInfo);
			}

			//Write Descriptors
			{
				auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
				vk::CommandBuffer writeConstantsCommand = cmdPool->AllocCommand(QueueType::eGraphics, "Write Descriptors");
				for (uint32_t batchID = 0; batchID < drawcallBatchs.size(); ++batchID)
				{
					auto& batch = drawcallBatchs[batchID];
					GPUPassBatchInfo& newBatchInfo = passInfo.m_Batches[batchID];
					newBatchInfo.m_ShaderBindingInstance.WriteShaderData(GetVulkanApplication(), *this, m_FrameBoundResourceManager, writeConstantsCommand, *batch.shaderArgs.get());
				}
				writeConstantsCommand.end();
				passInfo.m_CommandBuffers.push_back(writeConstantsCommand);
			}

			m_Passes.push_back(passInfo);
		}
	}
	
	void GPUGraphExecutor::PrepareResourceBarriers()
	{
		auto& graphStages = m_Graph->GetGraphStages();
		auto& renderPasses = m_Graph->GetRenderPasses();
		auto& dataTransfers = m_Graph->GetDataTransfers();
		auto& passIndices = m_Graph->GetPassIndices();

		castl::unordered_map<vk::Image, ResourceState> imageUsageFlagCache;
		castl::unordered_map<vk::Buffer, ResourceState> bufferUsageFlagCache;

		uint32_t currentRenderPassIndex = 0;
		uint32_t currentTransferPassIndex = 0;

		uint32_t passID = 0;
		for (auto stage : graphStages)
		{
			uint32_t realPassID = passIndices[passID];
			switch (stage)
			{
				case GPUGraph::EGraphStageType::eRenderPass:
				{
					auto& renderPass = renderPasses[realPassID];
					auto& renderPassData = m_Passes[realPassID];
					CA_ASSERT(realPassID == currentRenderPassIndex, "Render Pass Index Mismatch");
					++currentRenderPassIndex;

					auto& attachments = renderPass.GetAttachments();
					auto& drawcallBatchs = renderPass.GetDrawCallBatches();
					//Barriers
					{
						renderPassData.m_BarrierCollector.SetCurrentQueueFamilyIndex( GetQueueContext().GetGraphicsPipelineStageMask(), GetQueueContext().GetGraphicsQueueFamily());

						for (size_t batchID = 0; batchID < drawcallBatchs.size(); ++batchID)
						{
							auto& batch = drawcallBatchs[batchID];
							auto& batchData = renderPassData.m_Batches[batchID];
							PrepareVertexBuffersBarriers(renderPassData.m_BarrierCollector, bufferUsageFlagCache, batch, batchData, passID);
							PrepareShaderArgsResourceBarriers(renderPassData.m_BarrierCollector, imageUsageFlagCache, bufferUsageFlagCache, batch.shaderArgs.get(), passID);
							
							for (auto& bufferObject : batchData.m_ShaderBindingInstance.m_UniformBuffers)
							{
								renderPassData.m_BarrierCollector.PushBufferBarrier(bufferObject.buffer, ResourceUsage::eTransferDest, ResourceUsage::eFragmentRead | ResourceUsage::eVertexRead);
							}
						}
						for (size_t i = 0; i < attachments.size(); ++i)
						{
							auto& attachment = attachments[i];
							ResourceUsageFlags usageFlags = i == renderPass.GetDepthAttachmentIndex() ? ResourceUsage::eDepthStencilAttachment : ResourceUsage::eColorAttachmentOutput;
							UpdateImageDependency(passID, attachment, usageFlags, imageUsageFlagCache);
						}
					}
					break;
				}
				case GPUGraph::EGraphStageType::eTransferPass:
				{
					m_TransferPasses.emplace_back();
					GPUTransferInfo& transfersData = m_TransferPasses.back();
					transfersData.m_BarrierCollector.SetCurrentQueueFamilyIndex(GetQueueContext().GetTransferPipelineStageMask(), GetQueueContext().GetTransferQueueFamily());
					auto& transfersInfo = dataTransfers[realPassID];
					CA_ASSERT(realPassID == currentTransferPassIndex, "Render Pass Index Mismatch");
					++currentTransferPassIndex;
					for (auto& bufferUpload : transfersInfo.m_BufferDataUploads)
					{
						
						auto [bufferHandle, address, offset, size] = bufferUpload;
						if (bufferHandle.GetType() != BufferHandle::BufferType::Invalid)
						{
							ResourceUsageFlags usageFlags = ResourceUsage::eTransferDest;
							UpdateBufferDependency(passID, bufferHandle, usageFlags, bufferUsageFlagCache);
						}
					}
					for (auto& imageUpload : transfersInfo.m_ImageDataUploads)
					{
						auto [imageHandle, address, offset, size] = imageUpload;
						ResourceUsageFlags usageFlags = ResourceUsage::eTransferDest;
						UpdateImageDependency(passID, imageHandle, usageFlags, imageUsageFlagCache);
					}
					break;
				}
			}
			++passID;
		}
	
	}
	
	void GPUGraphExecutor::RecordGraph()
	{
		CA_ASSERT(m_Passes.size() == m_Graph->GetRenderPasses().size(), "Render Passe Count Mismatch");
		CA_ASSERT(m_TransferPasses.size() == m_Graph->GetDataTransfers().size(), "Transfer Pass Count Mismatch");
		auto& graphStages = m_Graph->GetGraphStages();
		auto& renderPasses = m_Graph->GetRenderPasses();
		auto& dataTransfers = m_Graph->GetDataTransfers();
		auto& passIndices = m_Graph->GetPassIndices();

		//InitialTransferPasses
		{
			for (auto& pair : m_ExternalResourceReleasingBarriers.queueFamilyToBarrierCollector)
			{
				auto& releaser = pair.second;
				auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
				vk::CommandBuffer externalResourceReleaseBarriers = cmdPool->AllocCommand(pair.first, "Data Transfer");
				releaser.barrierCollector.ExecuteReleaseBarrier(externalResourceReleaseBarriers);
				externalResourceReleaseBarriers.end();
				releaser.commandBuffer = externalResourceReleaseBarriers;
			}
		}


		uint32_t currentRenderPassIndex = 0;
		uint32_t currentTransferPassIndex = 0;

		uint32_t passID = 0;
		for (auto stage : graphStages)
		{
			uint32_t realPassID = passIndices[passID];
			switch (stage)
			{
				case GPUGraph::EGraphStageType::eRenderPass:
				{
					auto& renderPass = renderPasses[realPassID];
					auto& passData = m_Passes[realPassID];
					CA_ASSERT(realPassID == currentRenderPassIndex, "Render Pass Index Mismatch");
					++currentRenderPassIndex;

					auto& drawcallBatchs = renderPass.GetDrawCallBatches();
					auto& batchDatas = passData.m_Batches;
					CA_ASSERT(drawcallBatchs.size() == batchDatas.size(), "Batch Count Mismatch");

					auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
					vk::CommandBuffer renderPassCommandBuffer = cmdPool->AllocCommand(QueueType::eGraphics, "Render Pass");

					passData.m_BarrierCollector.ExecuteBarrier(renderPassCommandBuffer);
					renderPassCommandBuffer.beginRenderPass(
						vk::RenderPassBeginInfo{
							passData.m_RenderPassObject->GetRenderPass()
							, passData.m_FrameBufferObject->GetFramebuffer()
							, vk::Rect2D{{0, 0}, { passData.m_FrameBufferObject->GetWidth(), passData.m_FrameBufferObject->GetHeight() }}
							, passData.m_ClearValues
						}
					, vk::SubpassContents::eInline);

					renderPassCommandBuffer.setViewport(0, { vk::Viewport(0.0f, 0.0f, (float)passData.m_FrameBufferObject->GetWidth(), (float)passData.m_FrameBufferObject->GetHeight(), 0.0f, 1.0f) });
					renderPassCommandBuffer.setScissor(0, { vk::Rect2D({0, 0}, { passData.m_FrameBufferObject->GetWidth(), passData.m_FrameBufferObject->GetHeight() }) });

					
					CommandList_Impl commandList{ renderPassCommandBuffer };

					//castl::vector<uint32_t> bufferOffsets;
					for (uint32_t batchID = 0; batchID < drawcallBatchs.size(); ++batchID)
					{
						auto& batchData = batchDatas[batchID];
						auto& drawcallBatch = drawcallBatchs[batchID];

						renderPassCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, batchData.m_PSO->GetPipeline());
						
	/*					bufferOffsets.resize(batchData.m_ShaderBindingInstance.m_DescriptorSets.size());
						castl::fill(bufferOffsets.begin(), bufferOffsets.end(), 0);*/
						renderPassCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, batchData.m_PSO->GetPipelineLayout(), 0, batchData.m_ShaderBindingInstance.m_DescriptorSets, {});

						if (drawcallBatch.m_BoundIndexBuffer.GetType() != BufferHandle::BufferType::Invalid)
						{
							auto buffer = GetBufferHandleBufferObject(drawcallBatch.m_BoundIndexBuffer);
							renderPassCommandBuffer.bindIndexBuffer(buffer, drawcallBatch.m_IndexBufferOffset, EIndexBufferTypeTranslate(drawcallBatch.m_IndexBufferType));
						}

						for (auto& attributePair : batchData.m_VertexAttributeBindings)
						{
							auto foundVertexBuffer = drawcallBatch.m_BoundVertexBuffers.find(attributePair.first);
							if (foundVertexBuffer != drawcallBatch.m_BoundVertexBuffers.end())
							{
								auto buffer = GetBufferHandleBufferObject(foundVertexBuffer->second);
								renderPassCommandBuffer.bindVertexBuffers(attributePair.second.bindingIndex, { buffer }, { 0 });
							}
						}

						for (auto& drawFunc : drawcallBatch.m_DrawCommands)
						{
							drawFunc(commandList);
						}
					}

					renderPassCommandBuffer.endRenderPass();
					passData.m_BarrierCollector.ExecuteReleaseBarrier(renderPassCommandBuffer);
					renderPassCommandBuffer.end();
					passData.m_CommandBuffers.push_back(renderPassCommandBuffer);
					break;
				}
				case GPUGraph::EGraphStageType::eTransferPass:
				{
					auto& transfersInfo = dataTransfers[realPassID];
					GPUTransferInfo& transfersData = m_TransferPasses[realPassID];
					CA_ASSERT(realPassID == currentTransferPassIndex, "Render Pass Index Mismatch");
					++currentTransferPassIndex;

					auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
					vk::CommandBuffer dataTransferCommandBuffer = cmdPool->AllocCommand(QueueType::eTransfer, "Data Transfer");
					transfersData.m_BarrierCollector.ExecuteBarrier(dataTransferCommandBuffer);

					for (auto& bufferUpload : transfersInfo.m_BufferDataUploads)
					{
						auto [bufferHandle, address, offset, size] = bufferUpload;
						if (bufferHandle.GetType() != BufferHandle::BufferType::Invalid)
						{
							auto buffer = GetBufferHandleBufferObject(bufferHandle);
							if (buffer != vk::Buffer{ nullptr })
							{
								auto srcBuffer = m_FrameBoundResourceManager->CreateStagingBuffer(size, EBufferUsage::eDataSrc);
								{
									auto mappedSrcBuffer = m_FrameBoundResourceManager->memoryManager.ScopedMapMemory(srcBuffer.allocation);
									memcpy(mappedSrcBuffer.mappedMemory, address, size);
								}
								dataTransferCommandBuffer.copyBuffer(srcBuffer.buffer, buffer, vk::BufferCopy(0, offset, size));
							}
						}
					}

					for (auto& imageUpload : transfersInfo.m_ImageDataUploads)
					{
						auto [imageHandle, address, offset, size] = imageUpload;
						if (imageHandle.GetType() != ImageHandle::ImageType::Invalid)
						{
							auto image = GetTextureHandleImageObject(imageHandle);
							auto pDesc = GetTextureHandleDescriptor(imageHandle);
							if (image != vk::Image{nullptr})
							{
								auto srcBuffer = m_FrameBoundResourceManager->CreateStagingBuffer(size, EBufferUsage::eDataSrc);
								{
									auto mappedSrcBuffer = m_FrameBoundResourceManager->memoryManager.ScopedMapMemory(srcBuffer.allocation);
									memcpy(mappedSrcBuffer.mappedMemory, address, size);
								}

								//TODO: offset is not used here for now
								std::array<vk::BufferImageCopy, 1> bufferImageCopy = { GPUTextureDescriptorToBufferImageCopy(*pDesc) };
								dataTransferCommandBuffer.copyBufferToImage(srcBuffer.buffer
									, image
									, vk::ImageLayout::eTransferDstOptimal
									, bufferImageCopy);
							}
						}
					}	

					transfersData.m_BarrierCollector.ExecuteReleaseBarrier(dataTransferCommandBuffer);
					dataTransferCommandBuffer.end();
					transfersData.m_CommandBuffers.push_back(dataTransferCommandBuffer);
					break;
				}
			}
			++passID;
		}
	}
	void BufferSubAllocator::Allocate(CVulkanApplication& app, FrameBoundResourcePool* pResourcePool, GPUBufferDescriptor const& descriptor)
	{
		m_Buffers.clear();
		m_Buffers.reserve(passAllocationCount);
		for (int i = 0; i < passAllocationCount; ++i)
		{
			auto bufferObj = pResourcePool->CreateBufferWithMemory(descriptor);
			m_Buffers.push_back(castl::move(bufferObj));
		}
	}
	void ImageSubAllocator::Allocate(CVulkanApplication& app, FrameBoundResourcePool* pResourcePool, GPUTextureDescriptor const& descriptor)
	{
		m_Images.clear();
		m_Images.reserve(passAllocationCount);
		for (int i = 0; i < passAllocationCount; ++i)
		{
			auto imgObj = pResourcePool->CreateImageWithMemory(descriptor);
			m_Images.push_back(castl::move(imgObj));
		}
	}
	GPUGraphExecutor::ExternalResourceReleaser& GPUGraphExecutor::ExternalResourceReleasingBarriers::GetQueueFamilyReleaser(CVulkanApplication& app, uint32_t queueFamily)
	{
		auto found = queueFamilyToBarrierCollector.find(queueFamily);
		if (found == queueFamilyToBarrierCollector.end())
		{
			ExternalResourceReleaser newReleaser{};
			newReleaser.barrierCollector = VulkanBarrierCollector{ app.GetQueueContext().QueueFamilyIndexToPipelineStageMask(queueFamily)
				, queueFamily };
			newReleaser.commandBuffer = { nullptr };
			newReleaser.signalSemaphore = { nullptr };
			found = queueFamilyToBarrierCollector.insert(castl::make_pair(queueFamily, newReleaser)).first;
		}
		return found->second;
	}
}