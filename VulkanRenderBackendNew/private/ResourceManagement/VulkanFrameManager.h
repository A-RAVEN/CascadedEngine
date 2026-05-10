#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <ResourceManagement/VulkanCommandListManager.h>
#include <ResourceManagement/VulkanLinearMemoryManager.h>
#include <semaphore>
#include <CASTL/CAVector.h>
#include <CASTL/CAUniquePtr.h>
#include <CASTL/CAFunctional.h>

namespace graphics_backend
{
	struct WindowSync
	{
		vk::Semaphore acquireSemaphore;
		vk::Semaphore presentSemaphore;
	};

	class VulkanFrameBoundResourceManager : public VulkanSubobjectBase
	{
	public:
		VulkanFrameBoundResourceManager() = default;

		void Init();
		virtual void Release() override;
		void Reset();

		VulkanCommandListManager& GetCommandListManager() { return m_CommandListManager; }
		vk::DescriptorPool GetDescriptorPool() const { return m_DescriptorPool; }
		VulkanLinearMemoryManager& GetStagingMemoryManager() { return m_StagingMemoryManager; }
		vk::Fence GetDirectFence() const { return m_DirectFence; }
		vk::Fence GetComputeFence() const { return m_ComputeFence; }

		void EnsurePoolCapacity(uint32_t maxSets, castl::vector<vk::DescriptorPoolSize> const& poolSizes);
		void CreateFences();
		void ResetDescriptorPool();
		vk::Semaphore AllocCrossQueueSemaphore();
		vk::Semaphore AllocCrossQueueSemaphore();

	private:
		VulkanCommandListManager m_CommandListManager;
		vk::DescriptorPool m_DescriptorPool = nullptr;
		VulkanLinearMemoryManager m_StagingMemoryManager;
		vk::Fence m_DirectFence = nullptr;
		vk::Fence m_ComputeFence = nullptr;
		castl::vector<vk::Semaphore> m_CrossQueueSemaphores;
		int m_CrossQueueSemaphoreIndex = 0;

		uint32_t m_CurrentMaxSets = 0;
		castl::vector<vk::DescriptorPoolSize> m_CurrentPoolSizes;
	};

	class VulkanFrameContext : public VulkanSubobjectBase
	{
	public:
		VulkanFrameContext() : m_Semaphore(1) {}

		void Init();
		virtual void Release() override;
		void Reset();

		void Aquire();
		VulkanFrameBoundResourceManager& GetResourceManager() { return m_FrameBoundResourceManager; }

		void EnsureWindowSync(uint32_t index);
		WindowSync const& GetWindowSync(uint32_t index) const;
		uint32_t GetWindowSyncCount() const { return static_cast<uint32_t>(m_WindowSyncs.size()); }

	private:
		std::binary_semaphore m_Semaphore;
		VulkanFrameBoundResourceManager m_FrameBoundResourceManager;
		castl::vector<WindowSync> m_WindowSyncs;
		bool m_FirstFrame = true;
	};

	class VulkanGPUFrameManager : public VulkanSubobjectBase
	{
	public:
		using PFrameContext = castl::unique_ptr<VulkanFrameContext, castl::function<void(VulkanFrameContext*)>>;

		VulkanGPUFrameManager() = default;

		void Init(uint64_t maxFrameCount = 2);
		virtual void Release() override;

		PFrameContext AquireFrameContext();
		void WaitIdle();

	private:
		uint64_t m_FrameIndex = 0;
		uint64_t m_MaxFrameContexts = 2;
		castl::vector<castl::unique_ptr<VulkanFrameContext>> m_FrameContexts;
	};
}
