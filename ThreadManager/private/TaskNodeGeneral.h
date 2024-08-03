#pragma once
#include <CASTL/CAFunctional.h>
#include <CASTL/CAString.h>
#include <CASTL/CAArrayRef.h>

namespace thread_management
{
	class TaskScheduler;
	class TaskNodeBase
	{
	public:
		virtual void SetName(castl::string_view name) = 0;
		virtual void SetExplicitDepsCount(int32_t count) = 0;
		virtual void SignalExplicitDep() = 0;
		virtual void AddDepsOn(TaskNodeBase* predecessor) = 0;
	};

	class TaskNodeGeneral : virtual public TaskNodeBase
	{
	public:
		virtual TaskNodeGeneral* Func(castl::function<void()>& functor) = 0;
		virtual TaskNodeGeneral* Func(castl::function<void(TaskScheduler*)>& functor) = 0;
		TaskNodeGeneral* Name(castl::string_view const& name)
		{
			SetName(name);
			return this;
		}
		TaskNodeGeneral* DepsOn(TaskNodeBase* predecessor)
		{
			AddDepsOn(predecessor);
			return this;
		}
	};

	class TaskNodeParallel : virtual public TaskNodeBase
	{
	public:
		virtual TaskNodeParallel* Func(castl::function<void(uint32_t)>& functor) = 0;
		virtual TaskNodeParallel* Func(castl::function<void(uint32_t, TaskScheduler*)>& functor) = 0;
		TaskNodeParallel* Name(castl::string_view const& name)
		{
			SetName(name);
			return this;
		}
		TaskNodeParallel* DepsOn(TaskNodeBase* predecessor)
		{
			AddDepsOn(predecessor);
			return this;
		}
	};

	class TaskScheduler
	{
	public:
		virtual TaskNodeGeneral* NewTaskNode() = 0;
		virtual TaskNodeParallel* NewTaskNodeParallel() = 0;
		virtual void ExecuteAndWait(castl::array_ref<TaskNodeBase*> nodes) = 0;
	};

}