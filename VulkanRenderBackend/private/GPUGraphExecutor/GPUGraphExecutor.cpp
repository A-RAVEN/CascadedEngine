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

	//TODO: Same Sematics May be found in multiple attribute data
	CVertexInputDescriptor MakeVertexInputDescriptors(castl::map<castl::string, VertexInputsDescriptor> const& vertexAttributeDescs
		, castl::vector<ShaderCompilerSlang::ShaderVertexAttributeData> const& vertexAttributes
		, InputAssemblyStates assemblyStates
		, castl::map<castl::string, BufferHandle> const& boundVertexBuffers
		, castl::unordered_map<castl::string, VertexAttributeBindingData>& inoutBindingNameToIndex)
	{
		CVertexInputDescriptor result;
		result.assemblyStates = assemblyStates;

		result.m_PrimitiveDescriptions.resize(vertexAttributes.size());

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
					if (attribute.semanticName == attributeData.m_SematicName)
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
					}
				}
			}
			CA_ASSERT(attribBindingFound, "Attribute With Semantic Not Found");
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

	template<typename T>
	void UpdateResourceUsageFlags(castl::unordered_map<T, ResourceUsageFlags, cacore::hash<T>>& inoutResourceUsageFlagCache
		, T resource, ResourceUsageFlags flags)
	{
		auto found = inoutResourceUsageFlagCache.find(resource);
		if (found == inoutResourceUsageFlagCache.end())
		{
			inoutResourceUsageFlagCache.insert(castl::make_pair(resource, flags));
		}
		else
		{
			found->second = flags;
		}
	};

	template<typename T>
	ResourceUsageFlags GetResourceUsage(castl::unordered_map<T, ResourceUsageFlags, cacore::hash<T>>& inoutResourceUsageFlagCache
		, T resource
		, ResourceUsageFlags defaultFlags = ResourceUsage::eDontCare)
	{
		auto found = inoutResourceUsageFlagCache.find(resource);
		if (found != inoutResourceUsageFlagCache.end())
		{
			return found->second;
		}
		return defaultFlags;
	};

	void GPUGraphExecutor::PrepareVertexBuffersBarriers(VulkanBarrierCollector& inoutBarrierCollector
		, castl::unordered_map<vk::Buffer, ResourceUsageFlags, cacore::hash<vk::Buffer>>& inoutBufferUsageFlagCache
		, DrawCallBatch const& batch
		, GPUPassBatchInfo const& batchInfo
	)
	{
		if (batch.m_BoundIndexBuffer.GetType() != BufferHandle::BufferType::Invalid)
		{
			auto buffer = GetBufferHandleBufferObject(batch.m_BoundIndexBuffer);
			ResourceUsageFlags usageFlags = ResourceUsage::eVertexAttribute;
			ResourceUsageFlags originalFlags = GetResourceUsage(inoutBufferUsageFlagCache, buffer);
			if (originalFlags != usageFlags)
			{
				inoutBarrierCollector.PushBufferBarrier(buffer, originalFlags, usageFlags);
				UpdateResourceUsageFlags(inoutBufferUsageFlagCache, buffer, usageFlags);
			}
		}
		for (auto bindingPair : batchInfo.m_VertexAttributeBindings)
		{
			auto foundBuffer = batch.m_BoundVertexBuffers.find(bindingPair.first);
			if (foundBuffer != batch.m_BoundVertexBuffers.end())
			{
				auto buffer = GetBufferHandleBufferObject(foundBuffer->second);
				ResourceUsageFlags usageFlags = ResourceUsage::eVertexAttribute;
				ResourceUsageFlags originalFlags = GetResourceUsage(inoutBufferUsageFlagCache, buffer);
				if (originalFlags != usageFlags)
				{
					inoutBarrierCollector.PushBufferBarrier(buffer, originalFlags, usageFlags);
					UpdateResourceUsageFlags(inoutBufferUsageFlagCache, buffer, usageFlags);
				}
			}
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

		//Command Buffers
		m_GraphicsCommandBuffers.clear();
		m_WaitingSemaphores.clear();
	}

	void GPUGraphExecutor::PrepareShaderArgsResourceBarriers(VulkanBarrierCollector& inoutBarrierCollector
		, castl::unordered_map<vk::Image, ResourceUsageFlags, cacore::hash<vk::Image>>& inoutResourceUsageFlagCache
		, castl::unordered_map<vk::Buffer, ResourceUsageFlags, cacore::hash<vk::Buffer>>& inoutBufferUsageFlagCache
		, ShaderArgList const* shaderArgList)
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
					auto image = GetTextureHandleImageObject(imgHandle);
					auto pDesc = GetTextureHandleDescriptor(imgHandle);
					ResourceUsageFlags usageFlags = ResourceUsage::eVertexRead | ResourceUsage::eFragmentRead;
					ResourceUsageFlags originalFlags = GetResourceUsage(inoutResourceUsageFlagCache, image);
					if (originalFlags != usageFlags)
					{
						inoutBarrierCollector.PushImageBarrier(image, pDesc->format, originalFlags, usageFlags);
						UpdateResourceUsageFlags(inoutResourceUsageFlagCache, image, usageFlags);
					}
				}
			}
			for (auto& bufferPair : shaderArgs.GetBufferList())
			{
				auto& bufs = bufferPair.second;
				for (auto& buf : bufs)
				{
					auto buffer = GetBufferHandleBufferObject(buf);
					ResourceUsageFlags usageFlags = ResourceUsage::eVertexRead | ResourceUsage::eFragmentRead;
					ResourceUsageFlags originalFlags = GetResourceUsage(inoutBufferUsageFlagCache, buffer);
					if (originalFlags != usageFlags)
					{
						inoutBarrierCollector.PushBufferBarrier(buffer, originalFlags, usageFlags);
						UpdateResourceUsageFlags(inoutBufferUsageFlagCache, buffer, usageFlags);
					}
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
				m_GraphicsCommandBuffers.push_back(writeConstantsCommand);
			}

			m_Passes.push_back(passInfo);
		}
	}
	
	void GPUGraphExecutor::PrepareResourceBarriers()
	{
		auto& graphStages = m_Graph->GetGraphStages();
		auto& renderPasses = m_Graph->GetRenderPasses();
		auto& dataTransfers = m_Graph->GetDataTransfers();

		castl::unordered_map<vk::Image, ResourceUsageFlags, cacore::hash<vk::Image>> imageUsageFlagCache;
		castl::unordered_map<vk::Buffer, ResourceUsageFlags, cacore::hash<vk::Buffer>> bufferUsageFlagCache;

		uint32_t currentRenderPassIndex = 0;
		uint32_t currentTransferPassIndex = 0;

		for (auto stage : graphStages)
		{
			switch (stage)
			{
				case GPUGraph::EGraphStageType::eRenderPass:
				{
					auto& renderPass = renderPasses[currentRenderPassIndex];
					auto& renderPassData = m_Passes[currentRenderPassIndex];
					++currentRenderPassIndex;

					auto& attachments = renderPass.GetAttachments();
					auto& drawcallBatchs = renderPass.GetDrawCallBatches();
					//Barriers
					{
						renderPassData.m_BarrierCollector.SetCurrentQueueFamilyIndex(GetQueueContext().GetGraphicsQueueFamily());

						for (size_t batchID = 0; batchID < drawcallBatchs.size(); ++batchID)
						{
							auto& batch = drawcallBatchs[batchID];
							auto& batchData = renderPassData.m_Batches[batchID];
							PrepareVertexBuffersBarriers(renderPassData.m_BarrierCollector, bufferUsageFlagCache, batch, batchData);
							PrepareShaderArgsResourceBarriers(renderPassData.m_BarrierCollector, imageUsageFlagCache, bufferUsageFlagCache, batch.shaderArgs.get());
						}
						for (size_t i = 0; i < attachments.size(); ++i)
						{
							auto& attachment = attachments[i];
							auto image = GetTextureHandleImageObject(attachment);
							auto pDesc = GetTextureHandleDescriptor(attachment);
							ResourceUsageFlags usageFlags = i == renderPass.GetDepthAttachmentIndex() ? ResourceUsage::eDepthStencilAttachment : ResourceUsage::eColorAttachmentOutput;
							ResourceUsageFlags originalFlags = GetResourceUsage(imageUsageFlagCache, image);
							if (originalFlags != usageFlags)
							{
								renderPassData.m_BarrierCollector.PushImageBarrier(image, pDesc->format, originalFlags, usageFlags);
								UpdateResourceUsageFlags(imageUsageFlagCache, image, usageFlags);
							}
						}
					}
					break;
				}
				case GPUGraph::EGraphStageType::eTransferPass:
				{
					m_TransferPasses.emplace_back();
					GPUTransferInfo& transfersData = m_TransferPasses.back();
					auto& transfersInfo = dataTransfers[currentTransferPassIndex];
					++currentTransferPassIndex;
					for (auto& bufferUpload : transfersInfo.m_BufferDataUploads)
					{
						
						auto [bufferHandle, address, offset, size] = bufferUpload;
						if (bufferHandle.GetType() != BufferHandle::BufferType::Invalid)
						{
							auto buffer = GetBufferHandleBufferObject(bufferHandle);
							if(buffer != vk::Buffer{ nullptr })
							{
								ResourceUsageFlags usageFlags = ResourceUsage::eTransferDest;
								ResourceUsageFlags originalFlags = GetResourceUsage(bufferUsageFlagCache, buffer);
								if (originalFlags != usageFlags)
								{
									
									transfersData.m_BarrierCollector.PushBufferBarrier(buffer, originalFlags, usageFlags);
									UpdateResourceUsageFlags(bufferUsageFlagCache, buffer, usageFlags);
								}
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
							if (image != vk::Image{ nullptr })
							{
								ResourceUsageFlags usageFlags = ResourceUsage::eTransferDest;
								ResourceUsageFlags originalFlags = GetResourceUsage(imageUsageFlagCache, image);
								if (originalFlags != usageFlags)
								{
									transfersData.m_BarrierCollector.PushImageBarrier(image, pDesc->format, originalFlags, usageFlags);
									UpdateResourceUsageFlags(imageUsageFlagCache, image, usageFlags);
								}
							}
						}
					}
					break;
				}
			}
		}
	
	}
	
	void GPUGraphExecutor::RecordGraph()
	{
		CA_ASSERT(m_Passes.size() == m_Graph->GetRenderPasses().size(), "Render Passe Count Mismatch");
		CA_ASSERT(m_TransferPasses.size() == m_Graph->GetDataTransfers().size(), "Transfer Pass Count Mismatch");
		auto& graphStages = m_Graph->GetGraphStages();
		auto& renderPasses = m_Graph->GetRenderPasses();
		auto& dataTransfers = m_Graph->GetDataTransfers();


		uint32_t currentRenderPassIndex = 0;
		uint32_t currentTransferPassIndex = 0;

		for (auto stage : graphStages)
		{
			switch (stage)
			{
				case GPUGraph::EGraphStageType::eRenderPass:
				{
					auto& renderPass = renderPasses[currentRenderPassIndex];
					auto& passData = m_Passes[currentRenderPassIndex];
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

					CommandList_Impl commandList{ renderPassCommandBuffer };

					for (uint32_t batchID = 0; batchID < drawcallBatchs.size(); ++batchID)
					{
						auto& batchData = batchDatas[batchID];
						auto& drawcallBatch = drawcallBatchs[batchID];

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
					renderPassCommandBuffer.end();
					m_GraphicsCommandBuffers.push_back(renderPassCommandBuffer);
				
					break;
				}
				case GPUGraph::EGraphStageType::eTransferPass:
				{
					auto& transfersInfo = dataTransfers[currentTransferPassIndex];
					GPUTransferInfo& transfersData = m_TransferPasses.back();
					++currentTransferPassIndex;

					auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
					vk::CommandBuffer dataTransferCommandBuffer = cmdPool->AllocCommand(QueueType::eGraphics, "Data Transfer");
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

					dataTransferCommandBuffer.end();
					m_GraphicsCommandBuffers.push_back(dataTransferCommandBuffer);
					break;
				}
			}
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
}