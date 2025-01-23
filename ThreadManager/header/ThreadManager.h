#pragma once
#include <CASTL/CAString.h>
#include <CASTL/CAArrayRef.h>
#include <CASTL/CAFunctional.h>
#include <Hasher.h>
#include <CATimer/Timer.h>

namespace thread_management
{
	class CThreadManager;
	class TaskBase;
	class CTaskGraph;
	class TaskParallelFor;
	class CTask;

	enum class TaskNodeType
	{
		eGraph,
		eNode,
		eNodeParallel,
	};

	class TaskBase
	{
	public:
		virtual TaskNodeType GetType() const = 0;
	};

	class TaskScheduler
	{
	public:
		virtual CTask* NewTask() = 0;
		virtual TaskParallelFor* NewTaskParallelFor() = 0;
		virtual CTaskGraph* NewTaskGraph() = 0;
		virtual void Execute(castl::array_ref<TaskBase*> nodes, bool wait = false) = 0;
		virtual void WaitAll() = 0;
	};

	class CTask : public TaskBase
	{
	public:
		virtual TaskNodeType GetType() const override
		{
			return TaskNodeType::eNode;
		}

		virtual CTask* MainThread() = 0;
		virtual CTask* Thread(cacore::NameHash const& threadKey) = 0;
		virtual CTask* Name(cacore::NameHash name) = 0;
		virtual CTask* DependsOn(CTask* parentTask) = 0;
		virtual CTask* DependsOn(TaskParallelFor* parentTask) = 0;
		virtual CTask* DependsOn(CTaskGraph* parentTask) = 0;
		virtual CTask* WaitOnEvent(cacore::NameHash const& name) = 0;
		virtual CTask* SignalEvent(cacore::NameHash const& name) = 0;

		virtual CTask* Functor(castl::function<void()>&& functor) = 0;
	};

	class TaskParallelFor : public TaskBase
	{
	public:
		virtual TaskNodeType GetType() const override
		{
			return TaskNodeType::eNodeParallel;
		}

		virtual TaskParallelFor* Name(cacore::NameHash name) = 0;
		virtual TaskParallelFor* DependsOn(CTask* parentTask) = 0;
		virtual TaskParallelFor* DependsOn(TaskParallelFor* parentTask) = 0;
		virtual TaskParallelFor* DependsOn(CTaskGraph* parentTask) = 0;
		virtual TaskParallelFor* WaitOnEvent(cacore::NameHash const& name) = 0;
		virtual TaskParallelFor* SignalEvent(cacore::NameHash const& name) = 0;

		virtual TaskParallelFor* Functor(castl::function<void(uint32_t)> functor) = 0;
		virtual TaskParallelFor* JobCount(uint32_t jobCount) = 0;
	};

	class CTaskGraph : public TaskBase
	{
	public:
		virtual TaskNodeType GetType() const override
		{
			return TaskNodeType::eGraph;
		}

		virtual CTaskGraph* Name(cacore::NameHash name) = 0;
		virtual CTaskGraph* DependsOn(CTask* parentTask) = 0;
		virtual CTaskGraph* DependsOn(TaskParallelFor* parentTask) = 0;
		virtual CTaskGraph* DependsOn(CTaskGraph* parentTask) = 0;
		virtual CTaskGraph* WaitOnEvent(cacore::NameHash const& name) = 0;
		virtual CTaskGraph* SignalEvent(cacore::NameHash const& name) = 0;
		virtual CTaskGraph* MainThread() = 0;
		virtual CTaskGraph* Thread(cacore::NameHash const& threadKey) = 0;

		//延迟初始化函数
		virtual CTaskGraph* Func(castl::function<void(TaskScheduler*)> functor) = 0;
	};

	class CThreadManager
	{
	public:
		virtual void InitializeThreadCount(catimer::TimerSystem* timer, uint32_t threadNum, uint32_t dedicateThreadNum) = 0;
		virtual void SetDedicateThreadMapping(uint32_t dedicateThreadIndex, cacore::NameHash const& name) = 0;
		virtual void LoopFunction(castl::function<void(TaskScheduler*)> functor, cacore::NameHash const& waitingEvent) = 0;
		virtual castl::shared_ptr<TaskScheduler> NewScheduler() = 0;
		virtual void Run() = 0;
		virtual void LogStatus() const = 0;
	};
}