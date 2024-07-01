#pragma once
void* __cdecl operator new[](size_t size, const char* name, int flags, unsigned debugFlags, const char* file, int line);
void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* name, int flags, unsigned debugFlags, const char* file, int line);
#define CASTL_STD_COMPATIBLE
#include <ThreadManager.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CADeque.h>
#include <CASTL/CAVector.h>
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAArrayRef.h>
#include <CASTL/CASemaphore.h>
#include <CACore/header/ThreadSafePool.h>
#include "TaskNode.h"

namespace thread_management
{
	class TaskNodeAllocator;

	class CTask_Impl1 : public TaskNode, public CTask
	{
	public:
		CTask_Impl1(CTask_Impl1 const& other) = default;

		virtual CTask* MainThread() override;
		virtual CTask* Thread(cacore::HashObj<castl::string> const& threadKey) override;
		virtual CTask* Name(castl::string name) override;
		virtual CTask* DependsOn(CTask* parentTask) override;
		virtual CTask* DependsOn(TaskParallelFor* parentTask) override;
		virtual CTask* DependsOn(CTaskGraph* parentTask) override;
		virtual CTask* WaitOnEvent(castl::string const& name) override;
		virtual CTask* SignalEvent(castl::string const& name) override;
		std::shared_future<void> Run();

		virtual CTask* Functor(castl::function<void()>&& functor) override;
	public:
		CTask_Impl1(TaskBaseObject* owner, ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator);
		// 通过 CTask 继承
		 void Functor_Internal(castl::function<void()>&& functor);
		 void Initialize() {}
		 void Release();
	private:
		castl::function<void()> m_Functor;
		// 通过 TaskNode 继承
		virtual void Execute_Internal() override;
	};

	class TaskParallelFor_Impl : public TaskNode, public TaskParallelFor
	{
	public:
		TaskParallelFor_Impl(TaskParallelFor_Impl const& other) = default;

		virtual TaskParallelFor* Name(castl::string name) override;
		virtual TaskParallelFor* DependsOn(CTask* parentTask) override;
		virtual TaskParallelFor* DependsOn(TaskParallelFor* parentTask) override;
		virtual TaskParallelFor* DependsOn(CTaskGraph* parentTask) override;
		virtual TaskParallelFor* WaitOnEvent(castl::string const& name) override;
		virtual TaskParallelFor* SignalEvent(castl::string const& name) override;
		virtual TaskParallelFor* Functor(castl::function<void(uint32_t)> functor) override;
		virtual TaskParallelFor* JobCount(uint32_t jobCount) override;
		std::shared_future<void> Run();

	public:
		TaskParallelFor_Impl(TaskBaseObject* owner, ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator);

		void Initialize() {}
		void Release();

		// 通过 TaskNode 继承
		virtual void NotifyChildNodeFinish(TaskNode* childNode) override;
		virtual void Execute_Internal() override;
	private:
		castl::function<void(uint32_t)> m_Functor;
		std::atomic<uint32_t>m_PendingSubnodeCount{0};
		castl::vector<TaskNode*> m_TaskList;
	};

	class TaskGraph_Impl1 : public TaskNode, public CTaskGraph
	{
	public:
		TaskGraph_Impl1(TaskGraph_Impl1 const& other) = default;

		virtual CTaskGraph* Name(castl::string name) override;
		virtual CTaskGraph* DependsOn(CTask* parentTask) override;
		virtual CTaskGraph* DependsOn(TaskParallelFor* parentTask) override;
		virtual CTaskGraph* DependsOn(CTaskGraph* parentTask) override;
		virtual CTaskGraph* WaitOnEvent(castl::string const& name) override;
		virtual CTaskGraph* SignalEvent(castl::string const& name) override;
		virtual CTaskGraph* SetupFunctor(castl::function<void(CTaskGraph* thisGraph)> functor) override;
		virtual CTaskGraph* MainThread() override;
		virtual CTaskGraph* Thread(cacore::HashObj<castl::string> const& threadKey) override;
		virtual void AddResource(castl::shared_ptr<void> const& resource) override;
		std::shared_future<void> Run();

		virtual CTask* NewTask() override;
		virtual TaskParallelFor* NewTaskParallelFor() override;
		virtual CTaskGraph* NewTaskGraph() override;

	public:
		TaskGraph_Impl1(TaskBaseObject* owner, ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator);
		void Initialize() {}
		void Release();
	protected:
		// 通过 TaskNode 继承
		virtual void NotifyChildNodeFinish(TaskNode* childNode) override;
		virtual void Execute_Internal() override;
		virtual void SetupSubnodeDependencies() override;
	private:
		castl::function<void(CTaskGraph* thisGraph)> m_Functor = nullptr;
		castl::vector<TaskNode*> m_RootTasks;
		std::atomic<uint32_t>m_PendingSubnodeCount{0};

		std::mutex m_Mutex;
		eastl::deque<TaskNode*> m_SubTasks;
	};

	class TaskNodeAllocator
	{
	public:
		TaskNodeAllocator(ThreadManager_Impl1* owningManager);
		CTask_Impl1* NewTask(TaskBaseObject* owner);
		TaskParallelFor_Impl* NewTaskParallelFor(TaskBaseObject* owner);
		TaskGraph_Impl1* NewTaskGraph(TaskBaseObject* owner);
		void Release(TaskNode* node);
		void LogStatus() const;
	private:
		castl::atomic<uint32_t> m_Counter = 0;
		threadsafe_utils::TThreadSafePointerPool<CTask_Impl1> m_TaskPool;
		threadsafe_utils::TThreadSafePointerPool<TaskParallelFor_Impl> m_TaskParallelForPool;
		threadsafe_utils::TThreadSafePointerPool<TaskGraph_Impl1> m_TaskGraphPool;
		ThreadManager_Impl1* m_OwningManager;
	};

	class DedicateTaskQueue
	{
	public:
		DedicateTaskQueue() = default;
		DedicateTaskQueue(DedicateTaskQueue&& other) noexcept : DedicateTaskQueue(){}
		void Stop();
		void Reset();
		void WorkLoop();
		void EnqueueTaskNodes(castl::array_ref<TaskNode*> const& nodeDeque);
	private:
		void EnqueueTaskNodes_NoLock(castl::array_ref<TaskNode*> const& nodeDeque);
		std::mutex m_Mutex;
		bool m_Stop = false;
		eastl::deque<TaskNode*> m_Queue;
		std::condition_variable m_ConditionalVariable;
	};

	class DedicateThreadMap
	{
	public:
		uint32_t GetThreadIndex(cacore::HashObj<castl::string> const& name)
		{
			castl::lock_guard<castl::mutex> lock(m_Mutex);
			auto found = m_DedicateThreadMapping.find(name);
			if (found == m_DedicateThreadMapping.end())
			{
				uint32_t newID = m_DedicateThreadMapping.size();
				found = m_DedicateThreadMapping.insert(castl::make_pair(name, newID)).first;
			}
			return found->second;
		}
		void SetThreadIndex(cacore::HashObj<castl::string> const& name, uint32_t index)
		{
			castl::lock_guard<castl::mutex> lock(m_Mutex);
			m_DedicateThreadMapping[name] = index;
		}
	private:
		castl::mutex m_Mutex;
		castl::unordered_map<cacore::HashObj<castl::string>, uint32_t> m_DedicateThreadMapping;
	};

	class TaskNodeEventManager
	{
		struct TaskWaitList
		{
			castl::deque<TaskNode*> m_WaitingTasks;
			castl::deque<castl::pair<uint64_t, uint32_t>> m_WaitingFrames;
			uint64_t m_SignaledFrame = 0;
			void Signal(uint64_t signalFrame)
			{
				m_SignaledFrame = castl::max(m_SignaledFrame, signalFrame);
			}
		};
		castl::mutex m_Mutex;
		castl::unordered_map<cacore::HashObj<castl::string>, uint32_t> m_EventMap;
		castl::vector<TaskWaitList> m_EventWaitLists;
	public:
		void SignalEvent(ThreadManager_Impl1& threadManager, cacore::HashObj<castl::string> const& eventKey, uint64_t signalFrame);
		bool WaitEventDone(TaskNode* node);
	};


	class ThreadManager_Impl1 : public TaskBaseObject, public CThreadManager
	{
	public:
		virtual void InitializeThreadCount(catimer::TimerSystem* timer, uint32_t threadNum, uint32_t dedicateThreadNum) override;
		virtual void SetDedicateThreadMapping(uint32_t dedicateThreadIndex, cacore::HashObj<castl::string> const& name) override;
		CTask_Impl1* NewTask();
		TaskParallelFor_Impl* NewTaskParallelFor();
		TaskGraph_Impl1* NewTaskGraph();
		virtual void LogStatus() const override;
		virtual uint64_t GetCurrentFrame() const override { return m_Frames; }
		virtual void OneTime(castl::function<void(CTaskGraph*)> functor, castl::string const& waitingEvent) override;
		virtual void LoopFunction(castl::function<bool(CTaskGraph*)> functor, castl::string const& waitingEvent) override;
		virtual void Run() override;
		void Stop();
	public:
		ThreadManager_Impl1();
		~ThreadManager_Impl1();

		void SignalEvent(castl::string const& eventName, uint64_t signalFrame);

		void EnqueueOneTimeTasks();
		void EnqueueSetupTask();
		void EnqueueTaskNode(TaskNode* node);
		void EnqueueTaskNodes_Loop(castl::array_ref<TaskNode*> nodes);
		void EnqueueTaskNodes_GeneralThread(castl::array_ref<TaskNode*> nodes);
		void EnqueueTaskNode_GeneralThread(TaskNode* node);
		void EnqueueTaskNode_DedicateThread(TaskNode* node);

		virtual void NotifyChildNodeFinish(TaskNode* childNode) override;
	private:
		void ProcessingWorks();
		void ResetMainThread();
		void StopMainThread();
		void ProcessingWorksMainThread();
	private:
		//
		castl::function<bool(CTaskGraph*)> m_PrepareFunctor = nullptr;
		castl::string m_SetupEventName;

		castl::deque<TaskNode*> m_TaskQueue;
		castl::vector<std::thread> m_WorkerThreads;
		castl::vector<DedicateTaskQueue> m_DedicateTaskQueues;
		std::atomic_bool m_Stopped = false;
		castl::atomic<uint64_t> m_Frames = 0u;
		castl::mutex m_Mutex;
		castl::condition_variable m_ConditinalVariable;

		TaskNodeAllocator m_TaskNodeAllocator;
		DedicateThreadMap m_DedicateThreadMap;
		TaskNodeEventManager m_EventManager;

		castl::atomic<bool> m_WaitingIdle = false;
		castl::atomic<uint32_t> m_PendingTaskCount = 0;

		castl::vector<TaskNode*> m_InitializeTasks;
	};
}