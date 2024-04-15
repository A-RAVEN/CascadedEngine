#include <pch.h>
#include "GPUGraphExecutor.h"
#include <VulkanApplication.h>
#include <GPUGraphExecutor/ShaderBindingHolder.h>
#include <InterfaceTranslator.h>

namespace graphics_backend
{


	CVertexInputDescriptor MakeVertexInputDescriptors(castl::map<castl::string, VertexInputsDescriptor> const& vertexInputBindings
		, castl::vector<ShaderCompilerSlang::ShaderVertexAttributeData> const& vertexAttributes
		, InputAssemblyStates assemblyStates
		, castl::unordered_map<castl::string, VertexAttributeBindingData>& inoutBindingNameToIndex)
	{
		CVertexInputDescriptor result;
		result.assemblyStates = assemblyStates;

		result.m_PrimitiveDescriptions.resize(vertexAttributes.size());


		for (auto& attributeData : vertexAttributes)
		{
			for (auto descPair : vertexInputBindings)
			{
				for (auto& attribute : descPair.second.attributes)
				{
					if (attribute.semanticName == attributeData.m_SematicName)
					{
						auto found = inoutBindingNameToIndex.find(descPair.first);
						if (found == inoutBindingNameToIndex.end())
						{
							uint32_t bindingID = inoutBindingNameToIndex.size();
							found = inoutBindingNameToIndex.insert(castl::make_pair(descPair.first, VertexAttributeBindingData{ bindingID , descPair.second.stride, {}, descPair.second.perInstance })).first;
						}
						else
						{
						}
						found->second.attributes.push_back(VertexAttribute{ attributeData.m_Location , attribute.offset, attribute.format });
					}
				}
			}
		}

		return result;
	}

	void GPUGraphExecutor::PrepareGraph()
	{
		PrepareTextures();
		PrepareFrameBufferAndPSOs();
	}


	void GPUGraphExecutor::PrepareTextures()
	{
		auto& imageManager = m_Graph.GetImageManager();
		auto renderPasses = m_Graph.GetRenderPasses();
		for (auto& renderPass : renderPasses)
		{

			m_ImageManager.ResetAllocationIndices();
			auto& imageHandles = renderPass.GetAttachments();
			for (auto& img : imageHandles)
			{
				if (img.GetType() == ImageHandle::ImageType::Internal)
				{
					m_ImageManager.StateImageAllocation(img.GetName(), imageManager.GetDescriptorIndex(img.GetName()));
				}
			}
			auto& drawcallBatchs = renderPass.GetDrawCallBatches();
			castl::deque<ShaderArgList const*> shaderArgLists;
			for (auto& batch : drawcallBatchs)
			{
				auto& shaderArgs = batch.shaderArgs;
				if (batch.shaderArgs != nullptr)
				{
					shaderArgLists.push_back(shaderArgs.get());
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
						if (img.GetType() == ImageHandle::ImageType::Internal)
						{
							m_ImageManager.StateImageAllocation(img.GetName(), imageManager.GetDescriptorIndex(img.GetName()));
						}
					}
				}
			}
		}

		m_ImageManager.AllocTextures(GetVulkanApplication(), m_Graph.GetImageManager());
	}

	GPUTextureDescriptor const* GPUGraphExecutor::GetTextureHandleDescriptor(ImageHandle const& handle) const
	{
		auto imageType = handle.GetType();
		switch (imageType)
		{
			case ImageHandle::ImageType::Internal:
			{
				return m_Graph.GetImageManager().GetDescriptor(handle.GetName());
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

	vk::ImageView GPUGraphExecutor::GetTextureHandleImageView(ImageHandle const& handle) const
	{
		auto imageType = handle.GetType();
		switch (imageType)
		{
		case ImageHandle::ImageType::Internal:
		{
			return m_ImageManager.GetImageView(handle.GetName());
		}
		case ImageHandle::ImageType::External:
		{
			castl::shared_ptr<GPUTexture_Impl> texture = castl::static_shared_pointer_cast<GPUTexture_Impl>(handle.GetExternalManagedTexture());
			return texture->GetDefaultImageView();
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			castl::shared_ptr<CWindowContext> window = castl::static_shared_pointer_cast<CWindowContext>(handle.GetWindowHandle());
			return window->GetCurrentFrameImageView();
		}
		}
		return {};
	}

	vk::Image GPUGraphExecutor::GetTextureHandleImageObject(ImageHandle const& handle) const
	{
		auto imageType = handle.GetType();
		switch (imageType)
		{
		case ImageHandle::ImageType::Internal:
		{
			return m_ImageManager.GetImageObject(handle.GetName())->GetImage();
		}
		case ImageHandle::ImageType::External:
		{
			castl::shared_ptr<GPUTexture_Impl> texture = castl::static_shared_pointer_cast<GPUTexture_Impl>(handle.GetExternalManagedTexture());
			return texture->GetImageObject()->GetImage();
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			castl::shared_ptr<CWindowContext> window = castl::static_shared_pointer_cast<CWindowContext>(handle.GetWindowHandle());
			return window->GetCurrentFrameImage();
		}
		}
		return {};
	}

	template<typename T>
	void UpdateResourceUsageFlags(castl::unordered_map<T, ResourceUsageFlags>& inoutResourceUsageFlagCache
		, T resource, ResourceUsageFlags flags)
	{
		auto found = resourceUsageFlagCache.find(resource);
		if (found == resourceUsageFlagCache.end())
		{
			resourceUsageFlagCache.insert(castl::make_pair(resource, flags));
		}
		else
		{
			found->second = flags;
		}
	};

	template<typename T>
	ResourceUsageFlags GetResourceUsage(castl::unordered_map<T, ResourceUsageFlags>& inoutResourceUsageFlagCache
		, T resource
		, ResourceUsageFlags defaultFlags = ResourceUsage::eDontCare)
	{
		auto found = resourceUsageFlagCache.find(resource);
		if (found != resourceUsageFlagCache.end())
		{
			return found->second;
		}
		return defaultFlags;
	};

	void GPUGraphExecutor::PrepareShaderArgsImageBarriers(VulkanBarrierCollector& inoutBarrierCollector
		, castl::unordered_map<vk::Image, ResourceUsageFlags>& inoutResourceUsageFlagCache
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
					auto image = GetTextureHandleImageObject(img);
					auto pDesc = GetTextureHandleDescriptor(img);
					ResourceUsageFlags usageFlags = ResourceUsage::eVertexRead | ResourceUsage::eFragmentRead;
					inoutBarrierCollector.PushImageBarrier(image, pDesc->format, GetResourceUsage(inoutResourceUsageFlagCache, image), usageFlags);
					UpdateResourceUsageFlags(inoutResourceUsageFlagCache, image, usageFlags);
				}
			}
		}
	}

	void GPUGraphExecutor::PrepareFrameBufferAndPSOs()
	{
		auto renderPasses = m_Graph.GetRenderPasses();

		castl::unordered_map<vk::Image, ResourceUsageFlags> resourceUsageFlagCache;

		for (auto& renderPass : renderPasses)
		{
			auto& attachments = renderPass.GetAttachments();
			auto& drawcallBatchs = renderPass.GetDrawCallBatches();
			GPUPassInfo passInfo{};
			//RenderPass Object
			{
				RenderPassDescriptor renderPassDesc{};
				renderPassDesc.renderPassInfo.attachmentInfos.resize(attachments.size());
				renderPassDesc.renderPassInfo.subpassInfos.resize(1);

				for (size_t i = 0; i < attachments.size(); ++i)
				{
					auto& attachment = attachments[i];
					auto& attachmentInfo = renderPassDesc.renderPassInfo.attachmentInfos[i];
					auto pDesc = GetTextureHandleDescriptor(attachment);
					attachmentInfo.format = attachmentInfo.format;
					attachmentInfo.multiSampleCount = pDesc->samples;
					//TODO DO A Attachment Wise Version
					attachmentInfo.loadOp = renderPass.GetAttachmentLoadOp();
					attachmentInfo.storeOp = renderPass.GetAttachmentStoreOp();
					attachmentInfo.stencilLoadOp = renderPass.GetAttachmentLoadOp();
					attachmentInfo.stencilStoreOp = renderPass.GetAttachmentStoreOp();
					attachmentInfo.clearValue = renderPass.GetClearValue();
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
					frameBufferDesc.renderImageViews[i] = GetTextureHandleImageView(attachment);
					frameBufferDesc.renderpassObject = passInfo.m_RenderPassObject;
				}

				passInfo.m_FrameBufferObject = GetGPUObjectManager().GetFramebufferCache().GetOrCreate(frameBufferDesc);
			}

			//Barriers
			passInfo.m_BarrierCollector.SetCurrentQueueFamilyIndex(GetFrameCountContext().GetGraphicsQueueFamily());
			for (auto& batch : drawcallBatchs)
			{
				PrepareShaderArgsImageBarriers(passInfo.m_BarrierCollector, resourceUsageFlagCache, batch.shaderArgs.get());
			}
			for (size_t i = 0; i < attachments.size(); ++i)
			{
				auto& attachment = attachments[i];
				auto image = GetTextureHandleImageObject(attachment);
				auto pDesc = GetTextureHandleDescriptor(attachment);
				ResourceUsageFlags usageFlags = i == renderPass.GetDepthAttachmentIndex() ? ResourceUsage::eDepthStencilAttachment : ResourceUsage::eColorAttachmentOutput;
				passInfo.m_BarrierCollector.PushImageBarrier(image, pDesc->format, GetResourceUsage(resourceUsageFlagCache, image), usageFlags);
				UpdateResourceUsageFlags(resourceUsageFlagCache, image, usageFlags);
			}

			//Batch Info
			for (auto& batch : drawcallBatchs)
			{
				GPUPassBatchInfo newBatchInfo{};

				auto& psoDesc = batch.pipelineStateDesc;
				auto vertShader = GetGPUObjectManager().m_ShaderModuleCache.GetOrCreate(psoDesc.m_ShaderSet->GetShaderSourceInfo("spirv", ECompileShaderType::eVert));
				auto fragShader = GetGPUObjectManager().m_ShaderModuleCache.GetOrCreate(psoDesc.m_ShaderSet->GetShaderSourceInfo("spirv", ECompileShaderType::eFrag));

				//Shader Binding Holder
				newBatchInfo.m_ShaderBindingInstance.InitShaderBindings(GetVulkanApplication(), psoDesc.m_ShaderSet->GetShaderReflectionData());

				auto& vertexInputBindings = psoDesc.m_VertexInputBindings;
				auto& vertexAttributes = psoDesc.m_ShaderSet->GetShaderReflectionData().m_VertexAttributes;
				CVertexInputDescriptor vertexInputDesc = MakeVertexInputDescriptors(vertexInputBindings, vertexAttributes, psoDesc.m_InputAssemblyStates, newBatchInfo.m_VertexAttributeBindings);

				CPipelineObjectDescriptor psoDescObj;
				psoDescObj.vertexInputs = vertexInputDesc;
				psoDescObj.pso = psoDesc.m_PipelineStates;
				psoDescObj.shaderState = { vertShader, fragShader };
				psoDescObj.renderPassObject = passInfo.m_RenderPassObject;
				psoDescObj.descriptorSetLayouts = newBatchInfo.m_ShaderBindingInstance.m_DescriptorSetsLayouts;

				newBatchInfo.m_PSO = GetGPUObjectManager().GetPipelineCache().GetOrCreate(psoDescObj);

				passInfo.m_Batches.push_back(newBatchInfo);
			}

			m_Passes.push_back(passInfo);
		}
	}
}