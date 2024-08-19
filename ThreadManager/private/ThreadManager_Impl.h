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

	struct ThreadLocalData
	{
		castl::wstring threadName;
		uint32_t threadIndex;
		uint32_t queueIndex;
	};

	class TaskScheduler_Impl : public TaskScheduler, public TaskBaseObject
	{
	public:
		TaskScheduler_Impl(TaskBaseObject* owner, ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator);
		TaskScheduler_Impl(TaskScheduler_Impl const& other) = delete;
		void Execute(castl::array_ref<TaskNode*> nodes);
		void Finalize();
		bool IsFinished() const noexcept { return m_PendingTaskCount.load(castl::memory_order_seq_cst) <= 0; }
		void NotifyChildNodeFinish(TaskNode* childNode) override;
		virtual CTask* NewTask() override;
		virtual TaskParallelFor* NewTaskParallelFor() override;
		virtual CTaskGraph* NewTaskGraph() override;
		virtual void WaitAll() override;
		// 通过 TaskBaseObject 继承
		virtual uint64_t GetCurrentFrame() const override;
	private:
		TaskBaseObject* m_Owner;
		ThreadManager_Impl1* m_OwningManager;
		TaskNodeAllocator* m_Allocator;
		castl::atomic<int32_t> m_HoldingQueueID = -1;
		castl::atomic<int32_t> m_PendingTaskCount = 0;
		castl::vector<TaskNode*> m_SubTasks;


	};

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
		virtual CTask* Functor(castl::function<void()>&& functor) override;
	public:
		CTask_Impl1(ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator);
		// 通过 CTask 继承
		void Initialize() { Initialize_Internal(); }
		void Release();
	private:
		//castl::mutex m_Mutex;
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

	public:
		TaskParallelFor_Impl(ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator);

		void Initialize() { Initialize_Internal(); }
		void Release();

		// 通过 TaskNode 继承
		virtual void NotifyChildNodeFinish(TaskNode* childNode) override;
		virtual void Execute_Internal() override;
	private:
		castl::function<void(uint32_t)> m_Functor;
		castl::atomic<uint32_t>m_JobCount{0};
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
		virtual CTaskGraph* Func(castl::function<void(TaskScheduler*)> functor) override;
		virtual CTaskGraph* MainThread() override;
		virtual CTaskGraph* Thread(cacore::HashObj<castl::string> const& threadKey) override;
	public:
		TaskGraph_Impl1(ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator);
		void Initialize() { Initialize_Internal(); }
		void Release();
	protected:
		// 通过 TaskNode 继承
		virtual void NotifyChildNodeFinish(TaskNode* childNode) override;
		virtual void Execute_Internal() override;
	private:
		castl::function<void(TaskScheduler* scheduler)> m_ScheduleFunctor = nullptr;
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
		void NotifyAll();
		void Reset();
		//void InlineWorkLoop(TaskGraph_Impl1* taskGraph);
		void InlineWorkLoop(TaskScheduler_Impl* taskScheduler);
		void WorkLoop(ThreadLocalData const& threadLocalData);
		void EnqueueTaskNodes(castl::array_ref<TaskNode*> const& nodeDeque);
	private:
		void EnqueueTaskNodes_NoLock(castl::array_ref<TaskNode*> const& nodeDeque);
		castl::mutex m_Mutex;
		castl::atomic<bool> m_Stop = false;
		castl::deque<TaskNode*> m_Queue;
		castl::condition_variable m_ConditionalVariable;
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
		virtual void OneTime(castl::function<void(TaskScheduler*)> functor, castl::string const& waitingEvent) override;
		virtual void LoopFunction(castl::function<void(TaskScheduler*)> functor, castl::string const& waitingEvent) override;
		virtual void Run() override;
		void Stop();
		void WakeAll();

		DedicateTaskQueue& GetDedicateTaskQueue(uint32_t queueIndex) { return m_DedicateTaskQueues[queueIndex]; }
	public:
		ThreadManager_Impl1();
		~ThreadManager_Impl1();

		void SignalEvent(castl::string const& eventName, uint64_t signalFrame);

		void EnqueueOneTimeTasks();
		void EnqueueSetupTask();
		void EnqueueTaskNode(TaskNode* node);
		void EnqueueTaskNodes_Loop(castl::array_ref<TaskNode*> nodes);
		//void EnqueueTaskNodes_GeneralThread(castl::array_ref<TaskNode*> nodes);
		void EnqueueTaskNode_GeneralThread(TaskNode* node);
		void EnqueueTaskNode_DedicateThread(TaskNode* node);

		virtual void NotifyChildNodeFinish(TaskNode* childNode) override;
	private:
		//void ProcessingWorks(uint32_t threadID);
		void ResetMainThread();
		void StopMainThread();
		void ProcessingWorksMainThread();
	private:
		//
		castl::function<void(TaskScheduler*)> m_PrepareFunctor = nullptr;
		castl::string m_SetupEventName;

		//castl::deque<TaskNode*> m_TaskQueue;
		castl::vector<std::thread> m_WorkerThreads;
		castl::vector<DedicateTaskQueue> m_DedicateTaskQueues;
		//castl::atomic_bool m_Stopped = false;
		castl::atomic<uint64_t> m_Frames = 0u;
		castl::mutex m_Mutex;
		//castl::condition_variable m_ConditinalVariable;

		TaskNodeAllocator m_TaskNodeAllocator;
		DedicateThreadMap m_DedicateThreadMap;
		TaskNodeEventManager m_EventManager;

		castl::atomic<bool> m_WaitingIdle = false;
		castl::atomic<uint32_t> m_PendingTaskCount = 0;

		castl::vector<TaskNode*> m_InitializeTasks;

	};
}