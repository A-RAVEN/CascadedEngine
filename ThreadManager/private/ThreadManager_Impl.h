#pragma once
#include <future>
#include <deque>
#include <unordered_map>
#include <header/ThreadManager.h>
#include <CACore/header/ThreadSafePool.h>
#include "TaskNode.h"

namespace thread_management
{
	class TaskNodeAllocator;

	class CTask_Impl1 : public TaskNode, public CTask
	{
	public:
		virtual CTask* Name(std::string name) override;
		virtual CTask* DependsOn(CTask* parentTask) override;
		virtual CTask* DependsOn(TaskParallelFor* parentTask) override;
		virtual CTask* DependsOn(CTaskGraph* parentTask) override;
		virtual CTask* WaitOnEvent(std::string const& name, uint64_t waitingID) override;
		virtual CTask* SignalEvent(std::string const& name, uint64_t signalID) override;
		virtual std::shared_future<void> Run() override;

		virtual CTask* Functor(std::function<void()>&& functor) override;
	public:
		CTask_Impl1(TaskBaseObject* owner, ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator);
		// 通过 CTask 继承
		 void Functor_Internal(std::function<void()>&& functor);
		 void Initialize() {}
		 void Release();
	private:
		std::function<void()> m_Functor;
		// 通过 TaskNode 继承
		virtual void Execute_Internal() override;
	};

	class TaskParallelFor_Impl : public TaskNode, public TaskParallelFor
	{
	public:
		virtual TaskParallelFor* Name(std::string name) override;
		virtual TaskParallelFor* DependsOn(CTask* parentTask) override;
		virtual TaskParallelFor* DependsOn(TaskParallelFor* parentTask) override;
		virtual TaskParallelFor* DependsOn(CTaskGraph* parentTask) override;
		virtual TaskParallelFor* WaitOnEvent(std::string const& name, uint64_t waitingID) override;
		virtual TaskParallelFor* SignalEvent(std::string const& name, uint64_t signalID) override;
		virtual TaskParallelFor* Functor(std::function<void(uint32_t)> functor) override;
		virtual TaskParallelFor* JobCount(uint32_t jobCount) override;
		virtual std::shared_future<void> Run() override;

	public:
		TaskParallelFor_Impl(TaskBaseObject* owner, ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator);

		void Initialize() {}
		void Release();

		// 通过 TaskNode 继承
		virtual void NotifyChildNodeFinish(TaskNode* childNode) override;
		virtual void Execute_Internal() override;
	private:
		std::function<void(uint32_t)> m_Functor;
		std::atomic<uint32_t>m_PendingSubnodeCount{0};
		std::vector<TaskNode*> m_TaskList;
	};

	class TaskGraph_Impl1 : public TaskNode, public CTaskGraph
	{
	public:
		virtual CTaskGraph* Name(std::string name) override;
		virtual CTaskGraph* DependsOn(CTask* parentTask) override;
		virtual CTaskGraph* DependsOn(TaskParallelFor* parentTask) override;
		virtual CTaskGraph* DependsOn(CTaskGraph* parentTask) override;
		virtual CTaskGraph* WaitOnEvent(std::string const& name, uint64_t waitingID) override;
		virtual CTaskGraph* SignalEvent(std::string const& name, uint64_t signalID) override;
		virtual CTaskGraph* SetupFunctor(std::function<void(CTaskGraph* thisGraph)> functor) override;
		virtual std::shared_future<void> Run() override;

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
		std::function<void(CTaskGraph* thisGraph)> m_Functor = nullptr;
		std::vector<TaskNode*> m_RootTasks;
		std::atomic<uint32_t>m_PendingSubnodeCount{0};

		std::mutex m_Mutex;
		std::deque<TaskNode*> m_SubTasks;
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
		threadsafe_utils::TThreadSafePointerPool<CTask_Impl1> m_TaskPool;
		threadsafe_utils::TThreadSafePointerPool<TaskParallelFor_Impl> m_TaskParallelForPool;
		threadsafe_utils::TThreadSafePointerPool<TaskGraph_Impl1> m_TaskGraphPool;
		ThreadManager_Impl1* m_OwningManager;
	};

	class ThreadManager_Impl1 : public TaskBaseObject, public CThreadManager
	{
	public:
		virtual void InitializeThreadCount(uint32_t threadNum) override;
		virtual CTask* NewTask() override;
		virtual TaskParallelFor* NewTaskParallelFor() override;
		virtual CTaskGraph* NewTaskGraph() override;
		virtual void LogStatus() const override;
	public:
		ThreadManager_Impl1();
		~ThreadManager_Impl1();
		void EnqueueTaskNode(TaskNode* node);
		void EnqueueTaskNodes(std::vector<TaskNode*> const& nodeDeque);
		void SignalEvent(std::string const& eventName, uint64_t signalFrame);
		bool TryWaitOnEvent(TaskNode* node);
		virtual void NotifyChildNodeFinish(TaskNode* childNode) override;
	private:
		void ProcessingWorks(uint32_t threadId);
	private:
		//
		std::function<void(CThreadManager*)> m_PrepareFunctor = nullptr;

		std::deque<TaskNode*> m_TaskQueue;
		std::vector<std::thread> m_WorkerThreads;
		std::atomic_bool m_Stopped = false;
		std::mutex m_Mutex;
		std::condition_variable m_ConditinalVariable;

		TaskNodeAllocator m_TaskNodeAllocator;

		std::unordered_map<std::string, uint32_t> m_EventMap;
		struct TaskWaitList
		{
			std::deque<TaskNode*> m_WaitingTasks;
			std::deque<std::pair<uint64_t, uint32_t>> m_WaitingFrames;
			uint64_t m_SignaledFrame = 0;
			void Signal(uint64_t signalFrame);
		};
		std::vector<TaskWaitList> m_EventWaitLists;
	};
}