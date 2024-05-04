#pragma once
#include <CASTL/CAMutex.h>
#include <CASTL/CADeque.h>
//#include  <vulkan/vulkan.hpp>
#include "VulkanIncludes.h"
#include "VMA.h"
#include "RenderBackendSettings.h"
#include "VulkanApplicationSubobjectBase.h"
#include "CVulkanBufferObject.h"

namespace graphics_backend
{
#pragma region Forward Declaration
	class CVulkanApplication;
#pragma endregion

	template<typename T>
	class Internal_InterlockedQueue
	{
	public:
		template<typename TContainer>
		void Initialize(TContainer const& initializer)
		{
			castl::lock_guard<castl::mutex> lock(m_Mutex);
			m_Queue.clear();
			m_Queue.resize(initializer.size());
			castl::copy(initializer.begin(), initializer.end(), m_Queue.begin());
			m_Conditional.notify_one();
		}

		void Enqueue(T& newVar)
		{
			castl::lock_guard<castl::mutex> lock(m_Mutex);
			m_Queue.push_back(newVar);
			m_Conditional.notify_one();
		}
		T TryGetFront()
		{
			T result;
			{
				castl::unique_lock<castl::mutex> lock(m_Mutex);
				if (m_Queue.empty())
				{
					m_Conditional.wait(lock, [this]()
					{
						//TaskQueue不是空的，或者线程管理器已经停止，不再等待
						return !m_Queue.empty();
					});
				}
				CA_ASSERT(!m_Queue.empty(), "TaskQueue is empty");
				result = m_Queue.front();
				m_Queue.pop_front();
			}
			return result;
		}
	private:
		castl::mutex m_Mutex;
		castl::condition_variable m_Conditional;
		castl::deque<T> m_Queue;
	};

	class CVulkanFrameBoundCommandBufferPool : public VKAppSubObjectBaseNoCopy
	{
	public:
		CVulkanFrameBoundCommandBufferPool(CVulkanApplication& app);
		vk::CommandBuffer AllocateOnetimeCommandBuffer(const char* cmdName = "Default Cmd");
		vk::CommandBuffer AllocateMiscCommandBuffer(const char* cmdName = "Default Cmd");
		vk::CommandBuffer AllocateSecondaryCommandBuffer(const char* cmdName);
		void ResetCommandBufferPool();
		void CollectCommandBufferList(castl::vector<vk::CommandBuffer>& inoutCommandBufferList);
		uint32_t GetCommandFrame() const { return m_BoundingFrameID; }
		void Initialize();
		virtual void Release() override;
	private:
		// 通过 ApplicationSubobjectBase 继承
	private:
		class CommandBufferList
		{
		public:
			size_t m_AvailableCommandBufferIndex = 0;
			castl::vector<vk::CommandBuffer> m_CommandBufferList;
			vk::CommandBuffer AllocCommandBuffer(CVulkanFrameBoundCommandBufferPool& owner, bool secondary, const char* cmdName = "Default Cmd");
			void CollectCommandBufferList(castl::vector<vk::CommandBuffer>& inoutCommandBufferList);
			void ResetBufferList();
			void ClearBufferList();
		};
		vk::CommandPool m_CommandPool = nullptr;
		CommandBufferList m_CommandBufferList;
		CommandBufferList m_SecondaryCommandBufferList;
		uint32_t m_BoundingFrameID = 0;
		castl::vector<vk::CommandBuffer> m_MiscCommandBufferList;
	};

	class CVulkanThreadContext : public ApplicationSubobjectBase
	{
	public:
		CVulkanThreadContext(uint32_t threadId);
		CVulkanFrameBoundCommandBufferPool& GetCurrentFramePool();
		CVulkanFrameBoundCommandBufferPool& GetPoolByIndex(TIndex poolIndex);
		uint32_t GetThreadID() const { return m_ThreadID; }
		void DoReleaseContextResourceByIndex(TIndex releasingIndex);
	private:
		// 通过 ApplicationSubobjectBase 继承
		virtual void Initialize_Internal(CVulkanApplication const* owningApplication) override;
		virtual void Release_Internal() override;
	private:
		uint32_t m_ThreadID;
		castl::vector<CVulkanFrameBoundCommandBufferPool> m_FrameBoundCommandBufferPools;
	};
}
