#pragma once
#include <CASTL/CAString.h>
#include <CASTL/CASharedPtr.h>
#include <Hasher.h>
#include <CATimer/Timer.h>

namespace thread_management
{
	class CThreadManager;
	class CTaskGraph;
	class TaskParallelFor;
	class CTask;
	class CTaskUnsafe;

	class TaskUnsafeScheduler
	{
	public:
		virtual TaskUnsafe* NewTaskUnsafe() = 0;
		virtual TaskUnsafeScheduler* RunAndWait();

	};

	class TaskUnsafe
	{
	public:
		virtual ~TaskUnsafe() = default;
		TaskUnsafe() = default;
		TaskUnsafe(TaskUnsafe const& other) = delete;
		TaskUnsafe& operator=(TaskUnsafe const& other) = delete;
		TaskUnsafe(TaskUnsafe&& other) = delete;
		TaskUnsafe& operator=(TaskUnsafe&& other) = delete;

		virtual TaskUnsafe* MainThread() = 0;
		virtual TaskUnsafe* Thread(cacore::HashObj<castl::string> const& threadKey) = 0;
		virtual TaskUnsafe* Name(castl::string name) = 0;
		virtual TaskUnsafe* DependencyCount(uint32_t count) = 0;
		virtual TaskUnsafe* Functor(castl::function<void()>&& functor) = 0;
		virtual void DecreaseDependency(uint32_t decCount) = 0;
	};

	class TaskGraphUnsafe
	{
	public:
		virtual ~TaskGraphUnsafe() = default;
		TaskGraphUnsafe() = default;
		TaskGraphUnsafe(TaskGraphUnsafe const& other) = delete;
		TaskGraphUnsafe& operator=(TaskGraphUnsafe const& other) = delete;
		TaskGraphUnsafe(TaskGraphUnsafe&& other) = delete;
		TaskGraphUnsafe& operator=(TaskGraphUnsafe&& other) = delete;
		virtual TaskGraphUnsafe* MainThread() = 0;

		virtual TaskGraphUnsafe* MainThread() = 0;
		virtual TaskGraphUnsafe* Thread(cacore::HashObj<castl::string> const& threadKey) = 0;
		virtual TaskGraphUnsafe* Name(castl::string name) = 0;
		virtual TaskGraphUnsafe* SetupFunctor(castl::function<void(TaskUnsafeScheduler* scheduler)>&& functor) = 0;
	};

	class CTask
	{
	public:
		virtual ~CTask() = default;
		CTask() = default;
		CTask(CTask const& other) = delete;
		CTask& operator=(CTask const& other) = delete;
		CTask(CTask&& other) = delete;
		CTask& operator=(CTask&& other) = delete;

		virtual CTask* MainThread() = 0;
		virtual CTask* Thread(cacore::HashObj<castl::string> const& threadKey) = 0;
		virtual CTask* Name(castl::string name) = 0;
		virtual CTask* DependsOn(CTask* parentTask) = 0;
		virtual CTask* DependsOn(TaskParallelFor* parentTask) = 0;
		virtual CTask* DependsOn(CTaskGraph* parentTask) = 0;
		virtual CTask* WaitOnEvent(castl::string const& name) = 0;
		virtual CTask* SignalEvent(castl::string const& name) = 0;

		virtual CTask* Functor(castl::function<void()>&& functor) = 0;
	};

	class TaskParallelFor
	{
	public:
		virtual ~TaskParallelFor() = default;
		TaskParallelFor() = default;
		TaskParallelFor(TaskParallelFor const& other) = delete;
		TaskParallelFor& operator=(TaskParallelFor const& other) = delete;
		TaskParallelFor(TaskParallelFor&& other) = delete;
		TaskParallelFor& operator=(TaskParallelFor&& other) = delete;

		virtual TaskParallelFor* Name(castl::string name) = 0;
		virtual TaskParallelFor* DependsOn(CTask* parentTask) = 0;
		virtual TaskParallelFor* DependsOn(TaskParallelFor* parentTask) = 0;
		virtual TaskParallelFor* DependsOn(CTaskGraph* parentTask) = 0;
		virtual TaskParallelFor* WaitOnEvent(castl::string const& name) = 0;
		virtual TaskParallelFor* SignalEvent(castl::string const& name) = 0;

		virtual TaskParallelFor* Functor(castl::function<void(uint32_t)> functor) = 0;
		virtual TaskParallelFor* JobCount(uint32_t jobCount) = 0;
	};

	class CTaskGraph
	{
	public:
		virtual ~CTaskGraph() = default;
		CTaskGraph() = default;
		CTaskGraph(CTaskGraph const& other) = delete;
		CTaskGraph& operator=(CTaskGraph const& other) = delete;
		CTaskGraph(CTaskGraph && other) = delete;
		CTaskGraph& operator=(CTaskGraph && other) = delete;

		virtual CTaskGraph* Name(castl::string name) = 0;
		virtual CTaskGraph* DependsOn(CTask* parentTask) = 0;
		virtual CTaskGraph* DependsOn(TaskParallelFor* parentTask) = 0;
		virtual CTaskGraph* DependsOn(CTaskGraph* parentTask) = 0;
		virtual CTaskGraph* WaitOnEvent(castl::string const& name) = 0;
		virtual CTaskGraph* SignalEvent(castl::string const& name) = 0;
		virtual CTaskGraph* MainThread() = 0;
		virtual CTaskGraph* Thread(cacore::HashObj<castl::string> const& threadKey) = 0;

		virtual void AddResource(castl::shared_ptr<void> const& resource) = 0;
		template<typename T>
		void AddResource(castl::shared_ptr<T> const& resource)
		{
			AddResource(castl::static_pointer_cast<void>(resource));
		}

		//延迟初始化函数
		virtual CTaskGraph* SetupFunctor(castl::function<void(CTaskGraph* thisGraph)> functor) = 0;

		virtual CTask* NewTask() = 0;
		virtual TaskParallelFor* NewTaskParallelFor() = 0;
		virtual CTaskGraph* NewTaskGraph() = 0;
	};

	class CThreadManager
	{
	public:
		virtual ~CThreadManager() = default;
		CThreadManager() = default;
		CThreadManager(CThreadManager const& other) = delete;
		CThreadManager& operator=(CThreadManager const& other) = delete;
		CThreadManager(CThreadManager&& other) = delete;
		CThreadManager& operator=(CThreadManager&& other) = delete;

		virtual void InitializeThreadCount(catimer::TimerSystem* timer, uint32_t threadNum, uint32_t dedicateThreadNum) = 0;
		virtual void SetDedicateThreadMapping(uint32_t dedicateThreadIndex, cacore::HashObj<castl::string> const& name) = 0;
		virtual CTask* NewTask() = 0;
		virtual TaskParallelFor* NewTaskParallelFor() = 0;
		virtual CTaskGraph* NewTaskGraph() = 0;
		virtual void OneTime(castl::function<void(CTaskGraph*)> functor, castl::string const& waitingEvent) = 0;
		virtual void LoopFunction(castl::function<bool(CTaskGraph*)> functor, castl::string const& waitingEvent) = 0;
		virtual void Run() = 0;
		virtual void LogStatus() const = 0;
	};
}