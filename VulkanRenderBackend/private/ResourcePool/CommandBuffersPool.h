#pragma once
#include <CASTL/CAVector.h>
#include <GPUResources/GPUResourceInternal.h>
#include <VulkanApplicationSubobjectBase.h>
#include <GPUContexts/QueueContext.h>
#include <CASTL/CAThreadSafeQueue.h>

namespace graphics_backend
{
	class OneTimeCommandBufferPool : public VKAppSubObjectBaseNoCopy
	{
	public:
		OneTimeCommandBufferPool(CVulkanApplication& app);
		void Initialize();
		void Release();
		vk::CommandBuffer AllocCommand(QueueType queueType, const char* cmdName = "Default Cmd");
		vk::CommandBuffer AllocSecondaryCommand(const char* cmdName = "Default Cmd");
		void ResetCommandBufferPool();
	private:
		struct CommandBufferList
		{
			castl::vector<vk::CommandBuffer> commandBuffers;
			uint32_t lastUsedIndex = 0u;
			void Release()
			{
				commandBuffers.clear();
				lastUsedIndex = 0u;
			}
			void Reset()
			{
				lastUsedIndex = 0u;
			}
			void AddNewCommandBuffer(vk::CommandBuffer cmdBuffer)
			{
				commandBuffers.push_back(cmdBuffer);
			}
			bool TryGetNextCommandBuffer(vk::CommandBuffer& outCmdBuffer)
			{
				if (lastUsedIndex < commandBuffers.size())
				{
					outCmdBuffer = commandBuffers[lastUsedIndex];
					lastUsedIndex++;
					return true;
				}
				return false;
			}
		};
		struct SubCommandBufferPool
		{
			vk::CommandPool m_CommandPool;
			CommandBufferList m_PrimaryCommandBuffers;
			CommandBufferList m_SecondaryCommandBuffers;
			vk::CommandBuffer AllocPrimaryCommandBuffer(vk::Device device, const char* cmdName);
			vk::CommandBuffer AllocSecondaryCommandBuffer(vk::Device device, const char* cmdName);
			vk::CommandBuffer AllocateOnetimeCommandBufferInternal(vk::Device device, const char* cmdName, CommandBufferList& manageList, vk::CommandBufferLevel commandLevel);
			void ResetCommandBufferPool(vk::Device device);
			void Release(vk::Device device);
		};
		castl::vector<SubCommandBufferPool> m_CommandBufferPools;
		castl::array<int, QUEUE_TYPE_COUNT> m_QueueTypeToPoolIndex;
	};


	class CommandBufferThreadPool : public VKAppSubObjectBaseNoCopy
	{
	public:
		castl::shared_ptr<OneTimeCommandBufferPool> AquireCommandBufferPool();
		void ResetPool();
		void ReleasePool();
	private:
		castl::threadsafe_queue<OneTimeCommandBufferPool*> m_CommandBufferPools;
	};
}