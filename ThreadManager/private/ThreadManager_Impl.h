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
#include <CACore/CAModuleManager.h>

namespace thread_management
{
	class TaskNodeAllocator;

	struct ThreadLocalData
	{
		castl::wstring threadName;
		uint32_t threadIndex;
	};

	class TaskScheduler_Impl : public TaskScheduler, public TaskBaseObject
	{
	public:
		TaskScheduler_Impl(TaskBaseObject* owner, ThreadManager_Impl* owningManager, TaskNodeAllocator* allocator);
		TaskScheduler_Impl(TaskScheduler_Impl const& other) = delete;
		void Finalize();
		bool IsFinished();
		void NotifyChildNodeFinish(TaskNode* childNode) override;
		virtual void Execute(castl::array_ref<TaskBase*> nodes, bool wait) override;
		virtual CTask* NewTask() override;
		virtual TaskParallelFor* NewTaskParallelFor() override;
		virtual CTaskGraph* NewTaskGraph() override;
		virtual void WaitAll() override;
		// 通过 TaskBaseObject 继承
		virtual uint64_t GetCurrentFrame() const override;
		constexpr castl::shared_mutex& GetMutex() { return m_Mutex; }
	private:
		TaskBaseObject* m_Owner;
		ThreadManager_Impl* m_OwningManager;
		TaskNodeAllocator* m_Allocator;
		castl::atomic<int32_t> m_EntryWorkerID = -1;
		castl::shared_mutex m_Mutex;
		castl::atomic<int32_t> m_PendingTaskCount = 0;
		castl::vector<TaskBase*> m_SubTasks;


	};

	class CTask_Impl1 : public TaskNode, public CTask
	{
	public:
		CTask_Impl1(CTask_Impl1 const& other) = default;

		virtual CTask* MainThread() override;
		virtual CTask* Thread(cacore::NameHash const& threadKey) override;
		virtual CTask* Name(cacore::NameHash name) override;
		virtual CTask* DependsOn(CTask* parentTask) override;
		virtual CTask* DependsOn(TaskParallelFor* parentTask) override;
		virtual CTask* DependsOn(CTaskGraph* parentTask) override;
		virtual CTask* WaitOnEvent(cacore::NameHash const& name) override;
		virtual CTask* SignalEvent(cacore::NameHash const& name) override;
		virtual CTask* Functor(castl::function<void()>&& functor) override;
	public:
		CTask_Impl1(ThreadManager_Impl* owningManager, TaskNodeAllocator* allocator);
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
		virtual TaskParallelFor* Name(cacore::NameHash name) override;
		virtual TaskParallelFor* DependsOn(CTask* parentTask) override;
		virtual TaskParallelFor* DependsOn(TaskParallelFor* parentTask) override;
		virtual TaskParallelFor* DependsOn(CTaskGraph* parentTask) override;
		virtual TaskParallelFor* WaitOnEvent(cacore::NameHash const& name) override;
		virtual TaskParallelFor* SignalEvent(cacore::NameHash const& name) override;
		virtual TaskParallelFor* Functor(castl::function<void(uint32_t)> functor) override;
		virtual TaskParallelFor* JobCount(uint32_t jobCount) override;

	public:
		TaskParallelFor_Impl(ThreadManager_Impl* owningManager, TaskNodeAllocator* allocator);

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

		virtual CTaskGraph* Name(cacore::NameHash name) override;
		virtual CTaskGraph* DependsOn(CTask* parentTask) override;
		virtual CTaskGraph* DependsOn(TaskParallelFor* parentTask) override;
		virtual CTaskGraph* DependsOn(CTaskGraph* parentTask) override;
		virtual CTaskGraph* WaitOnEvent(cacore::NameHash const& name) override;
		virtual CTaskGraph* SignalEvent(cacore::NameHash const& name) override;
		virtual CTaskGraph* Func(castl::function<void(TaskScheduler*)> functor) override;
		virtual CTaskGraph* MainThread() override;
		virtual CTaskGraph* Thread(cacore::NameHash const& threadKey) override;
	public:
		TaskGraph_Impl1(ThreadManager_Impl* owningManager, TaskNodeAllocator* allocator);
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
		TaskNodeAllocator(ThreadManager_Impl* owningManager);
		CTask_Impl1* NewTask(TaskBaseObject* owner);
		TaskParallelFor_Impl* NewTaskParallelFor(TaskBaseObject* owner);
		TaskGraph_Impl1* NewTaskGraph(TaskBaseObject* owner);
		void Release(TaskBase* node);
		void LogStatus() const;
	private:
		castl::atomic<uint32_t> m_Counter = 0;
		threadsafe_utils::TThreadSafePointerPool<CTask_Impl1> m_TaskPool;
		threadsafe_utils::TThreadSafePointerPool<TaskParallelFor_Impl> m_TaskParallelForPool;
		threadsafe_utils::TThreadSafePointerPool<TaskGraph_Impl1> m_TaskGraphPool;
		ThreadManager_Impl* m_OwningManager;
	};

	//class DedicateTaskQueue
	//{
	//public:
	//	DedicateTaskQueue() = default;
	//	DedicateTaskQueue(DedicateTaskQueue&& other) noexcept : DedicateTaskQueue(){}
	//	void Stop();
	//	void NotifyAll();
	//	void Reset();
	//	//void InlineWorkLoop(TaskGraph_Impl1* taskGraph);
	//	void InlineWorkLoop(TaskScheduler_Impl* taskScheduler);
	//	void WorkLoop(ThreadLocalData const& threadLocalData);
	//	void EnqueueTaskNodes(castl::array_ref<TaskNode*> const& nodeDeque);
	//private:
	//	void EnqueueTaskNodes_NoLock(castl::array_ref<TaskNode*> const& nodeDeque);
	//	castl::mutex m_Mutex;
	//	castl::atomic<bool> m_Stop = false;
	//	castl::deque<TaskNode*> m_Queue;
	//	castl::condition_variable m_ConditionalVariable;
	//};


	class SharedTaskQueue
	{
	public:
		SharedTaskQueue(ThreadManager_Impl* owningManager, castl::array_ref<uint32_t> worker);
		SharedTaskQueue(SharedTaskQueue&& other) noexcept;
		SharedTaskQueue(SharedTaskQueue const& other) = delete;
		SharedTaskQueue& operator= (SharedTaskQueue const& other) = delete;
		constexpr castl::shared_mutex& GetMutex() { return m_QueueMutex; }
		constexpr castl::deque<TaskNode*>& GetQueue() { return m_Queue; }
		bool TryDeque(TaskNode*& outNode);
		void EnqueueTaskNodes(castl::array_ref<TaskNode*> const& nodeDeque);
	private:
		castl::shared_mutex m_QueueMutex;
		castl::deque<TaskNode*> m_Queue;
		castl::vector<size_t> m_WorkerIDs;
		size_t m_LastWorkerID = 0;
		ThreadManager_Impl* m_OwningManager = nullptr;
	};

	class GeneralTaskWorker
	{
	public:
		GeneralTaskWorker();
		GeneralTaskWorker(ThreadManager_Impl* owningManager);
		GeneralTaskWorker(GeneralTaskWorker&& other) noexcept;
		GeneralTaskWorker(GeneralTaskWorker const& other) = delete;
		GeneralTaskWorker& operator= (GeneralTaskWorker const& other) = delete;
		void Notify();
		void Stop();
		void Reset();
		void SetThreadLocalData(ThreadLocalData const& threadLocalData);
		void WorkLoopWithThreadLocalData(ThreadLocalData const& threadLocalData);
		void WorkLoop();
		void InlineWorkLoop(TaskScheduler_Impl* taskScheduler);
		void AddQueue(size_t queueID);
	private:
		castl::mutex m_WorkerMutex;
		castl::condition_variable m_WorkerConditionalVariable;
		bool m_Pause = false;
		bool m_Stop = false;
		castl::vector<size_t> m_Queues;
		ThreadManager_Impl* m_OwningManager = nullptr;
	};

	class DedicateThreadMap
	{
	public:
		uint32_t GetThreadIndex(cacore::NameHash const& name)
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
		void SetThreadIndex(cacore::NameHash const& name, uint32_t index)
		{
			castl::lock_guard<castl::mutex> lock(m_Mutex);
			m_DedicateThreadMapping[name] = index;
		}
	private:
		castl::mutex m_Mutex;
		castl::unordered_map<cacore::NameHash, uint32_t> m_DedicateThreadMapping;
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
				m_SignaledFrame = (castl::max)(m_SignaledFrame, signalFrame);
			}
		};
		castl::mutex m_Mutex;
		castl::unordered_map<cacore::NameHash, uint32_t> m_EventMap;
		castl::vector<TaskWaitList> m_EventWaitLists;
	public:
		void SignalEvent(ThreadManager_Impl& threadManager, cacore::NameHash const& eventKey, uint64_t signalFrame);
		bool WaitEventDone(TaskNode* node);
	};


	class ThreadManager_Impl : public TaskBaseObject, public CThreadManager
	{
	public:
		void Init(cacore::IModuleManager* pManager);
		virtual void InitializeThreadCount(catimer::TimerSystem* timer, uint32_t threadNum) override;
		void AddTaskQueue(cacore::NameHash const& name, castl::array_ref<uint32_t> threadIDs) override;
		CTask_Impl1* NewTask();
		TaskParallelFor_Impl* NewTaskParallelFor();
		TaskGraph_Impl1* NewTaskGraph();
		virtual void LogStatus() const override;
		virtual uint64_t GetCurrentFrame() const override { return m_Frames; }
		virtual void LoopFunction(castl::function<void(TaskScheduler*)> functor, cacore::NameHash const& waitingEvent) override;
		virtual castl::shared_ptr<TaskScheduler> NewScheduler() override;
		virtual void Run() override;
		void Stop();
		void WakeAll();
		bool IsRunning() noexcept;
		constexpr SharedTaskQueue& GetSharedTaskQueue(size_t queueID) { return m_SharedTaskQueues[queueID]; }
		constexpr GeneralTaskWorker& GetTaskWorker(size_t workerID) { return m_TaskWorkers[workerID]; }

		//DedicateTaskQueue& GetDedicateTaskQueue(uint32_t queueIndex) { return m_DedicateTaskQueues[queueIndex]; }
	public:
		ThreadManager_Impl();
		~ThreadManager_Impl();

		void SignalEvent(cacore::NameHash const& eventName, uint64_t signalFrame);

		void EnqueueSetupTask();
		void EnqueueTaskNode(TaskNode* node);
		void EnqueueTaskNodes_Loop(castl::array_ref<TaskNode*> nodes);
		//void EnqueueTaskNodes_GeneralThread(castl::array_ref<TaskNode*> nodes);
		//void EnqueueTaskNode_GeneralThread(TaskNode* node);
		//void EnqueueTaskNode_DedicateThread(TaskNode* node);

		virtual void NotifyChildNodeFinish(TaskNode* childNode) override;

		constexpr castl::shared_mutex& GetMutex() { return m_Mutex; }
	private:
		//void ProcessingWorks(uint32_t threadID);
		void ResetMainThread();
		void StopMainThread();
		void ProcessingWorksMainThread();
	private:
		//
		castl::function<void(TaskScheduler*)> m_PrepareFunctor = nullptr;
		cacore::NameHash m_SetupEventName;

		castl::vector<std::thread> m_WorkerThreads;
		//castl::vector<DedicateTaskQueue> m_DedicateTaskQueues;
		castl::shared_mutex m_Mutex;
		castl::atomic<uint64_t> m_Frames = 0u;
		castl::atomic_bool m_Running = true;

		TaskNodeAllocator m_TaskNodeAllocator;
		DedicateThreadMap m_DedicateThreadMap;
		TaskNodeEventManager m_EventManager;

		castl::vector<GeneralTaskWorker> m_TaskWorkers;
		castl::vector<SharedTaskQueue> m_SharedTaskQueues;

		castl::atomic<bool> m_WaitingIdle = false;
		castl::atomic<uint32_t> m_PendingTaskCount = 0;

	};
}