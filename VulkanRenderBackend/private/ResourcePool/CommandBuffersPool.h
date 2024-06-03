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
		OneTimeCommandBufferPool(CVulkanApplication& app, uint32_t index);
		void Initialize();
		void Release();
		vk::CommandBuffer AllocCommand(QueueType queueType, const char* cmdName = "Default Cmd");
		vk::CommandBuffer AllocCommand(uint32_t queueFamilyID, const char* cmdName = "Default Cmd");
		vk::CommandBuffer AllocSecondaryCommand(const char* cmdName = "Default Cmd");
		void ResetCommandBufferPool();
	private:
		friend class CommandBufferThreadPool;
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
		uint32_t m_Index;
	};


	class CommandBufferThreadPool : public VKAppSubObjectBaseNoCopy
	{
	public:
		CommandBufferThreadPool(CVulkanApplication& app);
		CommandBufferThreadPool(CommandBufferThreadPool&& other) noexcept;
		castl::shared_ptr<OneTimeCommandBufferPool> AquireCommandBufferPool();
		void ResetPool();
		void ReleasePool();
	private:
		castl::mutex m_Mutex;
		castl::condition_variable m_ConditionVariable;
		castl::deque<int> m_AvailablePools;
		castl::vector<OneTimeCommandBufferPool> m_CommandBufferPools;
	};
}