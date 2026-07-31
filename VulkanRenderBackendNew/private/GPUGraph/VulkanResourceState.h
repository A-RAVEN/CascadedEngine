#pragma once
#include <Utils/VulkanIncludes.h>
#include <Common.h>

namespace graphics_backend
{
	struct VulkanResourceState
	{
		vk::AccessFlags accessFlags;
		vk::PipelineStageFlags stageFlags;
		vk::ImageLayout imageLayout;
		EGPUQueueType queueType;
		bool isImage;

		bool Write() const {
			return (accessFlags & (vk::AccessFlagBits::eColorAttachmentWrite |
				vk::AccessFlagBits::eDepthStencilAttachmentWrite |
				vk::AccessFlagBits::eShaderWrite |
				vk::AccessFlagBits::eTransferWrite)) != vk::AccessFlags{};
		}

		bool CompatibleToCombine(VulkanResourceState const& other) const {
			return queueType == other.queueType;
		}

		void Combine(VulkanResourceState const& other) {
			accessFlags |= other.accessFlags;
			stageFlags |= other.stageFlags;
		}

		bool hasDirectQueue() const { return queueType == EGPUQueueType::eDirect; }
		bool hasComputeQueue() const { return queueType == EGPUQueueType::eCompute; }
		bool isSharedBetweenQueues() const { return false; }

		static VulkanResourceState InitializedImageState() {
			return { vk::AccessFlagBits::eNone, vk::PipelineStageFlagBits::eTopOfPipe,
				vk::ImageLayout::eUndefined, EGPUQueueType::eDirect, true };
		}
		static VulkanResourceState InitializedBufferState() {
			return { vk::AccessFlagBits::eNone, vk::PipelineStageFlagBits::eTopOfPipe,
				vk::ImageLayout::eUndefined, EGPUQueueType::eDirect, false };
		}
	};
}
