#include "TaskNodeGeneral.h"
#include <CASTL/CAVector.h>
#include <CASTL/CAMutex.h>
#include <CASTL/CAAtomic.h>
#include <CASTL/CADeque.h>

namespace thread_management
{
	class RootTaskAllocator;
	class TaskObjectBase;
	class TaskNodeBase_Impl;
	class TaskNodeGeneral_Impl;

	enum class ETaskNodeType
	{
		eGeneral,
		eParallel,
	};

	class ThreadLocalData
	{
	public:
		castl::string threadName;
		uint32_t threadID;
	};

	class TaskAllocator
	{
	public:
		virtual TaskNodeGeneral_Impl* NewGeneralTaskNode() = 0;
		virtual void ReleaseTaskNode(TaskNodeBase_Impl* node) = 0;
	};

	class NodeLocalTaskAllocator : public TaskAllocator
	{
	public:
		NodeLocalTaskAllocator(TaskNodeBase_Impl* owningNode, RootTaskAllocator* rootAllocator) : m_OwningNode(owningNode), m_RootAllocator(rootAllocator)
		{
		}
		virtual TaskNodeGeneral_Impl* NewGeneralTaskNode() override
		{
			auto result = m_RootAllocator->NewGeneralTaskNode();
			result->SetTaskQueue(m_OwningNode->GetTaskQueue());
			result->SetOwner(m_OwningNode);
			return result;
		}
		virtual void ReleaseTaskNode(TaskNodeBase_Impl* node) override
		{
			m_RootAllocator->ReleaseTaskNode(node);
		}
	private:
		TaskNodeBase_Impl* m_OwningNode;
		RootTaskAllocator* m_RootAllocator;
	};

	class TaskQueue
	{
	public:
		castl::condition_variable& GetConditionalVariable()
		{
			return m_ConditionalVariable;
		}
		castl::mutex& GetMutex()
		{
			return m_Mutex;
		}
		bool Empty() const
		{
			return m_Queue.empty();
		}
		castl::deque<TaskNodeBase_Impl*>& GetQueue()
		{
			return m_Queue;
		}
		void Enqueue(TaskNodeBase_Impl* node)
		{
			{
				castl::unique_lock<castl::mutex> lock(m_Mutex);
				m_Queue.push_back(node);
			}
			m_ConditionalVariable.notify_one();
		}
	private:
		castl::deque<TaskNodeBase_Impl*> m_Queue;
		castl::condition_variable m_ConditionalVariable;
		castl::mutex m_Mutex;
	};

	class TaskWorker
	{
	public:
		class TaskThreadExecuteProxy : public TaskObjectBase
		{
		public:
			TaskThreadExecuteProxy(TaskObjectBase* baseObj, castl::array_ref<TaskNodeBase*> nodes)
			{
				SetOwner(baseObj);
				m_TaskCount = nodes.size();
				for(TaskNodeBase* taskNode : nodes)
				{
					TaskNodeBase_Impl* taskNodeImpl = dynamic_cast<TaskNodeBase_Impl*>(taskNode);
					taskNodeImpl->SetOwner(this);
				}
			}

			bool AllTaskDone() const
			{
				return m_TaskCount <= 0;
			}

			virtual TaskAllocator* GetAllocator() override
			{
				return GetOwner()->GetAllocator();
			}

			virtual void OnChildNodeFinish(TaskNodeBase_Impl* childNode) override
			{
				GetAllocator()->ReleaseTaskNode(childNode);
				--m_TaskCount;
			}
			
		private:
			castl::atomic<int32_t> m_TaskCount = 0;
		};

	public:
		void WorkLoopWhileWaiting(TaskThreadExecuteProxy& proxy)
		{
			while (!(proxy.AllTaskDone() || m_Stop))
			{
				castl::unique_lock<castl::mutex> lock(p_TargetTaskQueue->GetMutex());
				p_TargetTaskQueue->GetConditionalVariable().wait(lock, [this, &proxy]()
					{
						return m_Stop || proxy.AllTaskDone() || !p_TargetTaskQueue->Empty();
					});
				if (m_Stop || p_TargetTaskQueue->Empty() || proxy.AllTaskDone())
				{
					lock.unlock();
					continue;
				}
				TaskNodeBase_Impl* taskNode = p_TargetTaskQueue->GetQueue().front();
				p_TargetTaskQueue->GetQueue().pop_front();
				lock.unlock();
				taskNode->Execute();
			}
		}
		void WorkLoop()
		{
			while (!m_Stop)
			{
				castl::unique_lock<castl::mutex> lock(p_TargetTaskQueue->GetMutex());
				p_TargetTaskQueue->GetConditionalVariable().wait(lock, [this]()
					{
						return m_Stop || !p_TargetTaskQueue->Empty();
					});
				if (m_Stop || p_TargetTaskQueue->Empty())
				{
					lock.unlock();
					continue;
				}
				TaskNodeBase_Impl* taskNode = p_TargetTaskQueue->GetQueue().front();
				p_TargetTaskQueue->GetQueue().pop_front();
				lock.unlock();
				taskNode->Execute();
			}
		}
	private:
		bool m_Stop = false;
		TaskQueue* p_TargetTaskQueue;
	};

	class TaskQueueManager
	{
	public:
		void EnqueueSingleTaskNode(TaskNodeBase_Impl* node)
		{
			EnqueueSingleTaskNodeNoLock(node);
		}

		void EnqueueTaskNodes(castl::array_ref<TaskNodeBase*> nodes, bool wait)
		{
			if (wait)
			{
				EnqueueTasksNoLockAndWait(nodes);
			}
			else
			{
				EnqueueTasksNoLockNoWait(nodes);
			}
		}

		TaskWorker& GetCurrentThreadWorker()
		{
			return m_TaskWorkers[s_ThreadLocalData.threadID];
		}

	private:
		thread_local static ThreadLocalData s_ThreadLocalData;
		constexpr static size_t SHARED_THREAD_ID = 0;
		constexpr static size_t MAIN_THREAD_ID = 0;

		void EnqueueSingleTaskNodeNoLock(TaskNodeBase_Impl* node)
		{
			//m_TaskNodes.push_back(node);
			m_TaskQueues[SHARED_THREAD_ID].Enqueue(node);
		}

		void EnqueueTasksNoLockAndWait(castl::array_ref<TaskNodeBase*> nodes)
		{
			TaskWorker::TaskThreadExecuteProxy proxy{ nodes };
			for (TaskNodeBase* taskNode : nodes)
			{
				TaskNodeBase_Impl* taskNodeImpl = dynamic_cast<TaskNodeBase_Impl*>(taskNode);
				
				if (taskNodeImpl->ReadyToExecute())
				{
					EnqueueSingleTaskNodeNoLock(taskNodeImpl);
				}
			}
			//等待列表中的任务完成，同时此线程也会执行任务
			GetCurrentThreadWorker().WorkLoopWhileWaiting(proxy);
		}

		void EnqueueTasksNoLockNoWait(castl::array_ref<TaskNodeBase*> nodes)
		{
			for (TaskNodeBase* taskNode : nodes)
			{
				TaskNodeBase_Impl* taskNodeImpl = dynamic_cast<TaskNodeBase_Impl*>(taskNode);
				if (taskNodeImpl->ReadyToExecute())
				{
					EnqueueSingleTaskNodeNoLock(taskNodeImpl);
				}
			}
		}

		castl::vector<TaskWorker> m_TaskWorkers;
		castl::vector<TaskQueue> m_TaskQueues;
	};

	class TaskObjectBase
	{
	public:
		void SetOwner(TaskObjectBase* owner)
		{
			m_Owner = owner;
		}
		TaskObjectBase* GetOwner()
		{
			return m_Owner;
		}
		virtual TaskAllocator* GetAllocator() = 0;
		virtual void OnChildNodeFinish(TaskNodeBase_Impl* childNode)
		{
			GetAllocator()->ReleaseTaskNode(childNode);
		}
	private:
		TaskObjectBase* m_Owner = nullptr;
	};

	class TaskNodeBase_Impl : public virtual TaskNodeBase, public TaskObjectBase
	{
	public:
		TaskNodeBase_Impl(ETaskNodeType nodeType
			, RootTaskAllocator* rootAllocator) : m_NodeType(nodeType), m_LocalAllocator(this, rootAllocator)
		{
		}

		virtual void SetName(castl::string_view name) override
		{
			m_Name = name;
		}
		virtual void SetExplicitDepsCount(int32_t count) override
		{
			m_ExplicitDepsCount = count;
		}
		virtual void SignalExplicitDep() override
		{
			--m_ExplicitDepsCount;
			CheckEnqueueSelf();
		}
		virtual void AddDepsOn(TaskNodeBase* predecessor) override
		{
			TaskNodeBase_Impl* pred = dynamic_cast<TaskNodeBase_Impl*>(predecessor);
			pred->m_Successors.push_back(this);
			++m_DepsCount;
		}

		void SignalDep()
		{
			--m_DepsCount;
			CheckEnqueueSelf();
		}

		void SetTaskQueue(TaskQueueManager* taskQueue)
		{
			m_TaskQueue = taskQueue;
		}

		TaskQueueManager* GetTaskQueue()
		{
			return m_TaskQueue;
		}

		//Called By Thread Worker
		virtual void Execute() = 0;

		bool ReadyToExecute() const
		{
			return m_ExplicitDepsCount <= 0 && m_DepsCount <= 0;
		}

		void CheckEnqueueSelf()
		{
			if (ReadyToExecute())
			{
				m_TaskQueue->EnqueueSingleTaskNode(this);
			}
		}

		// 通过 TaskObjectBase 继承
		TaskAllocator* GetAllocator() override
		{
			return &m_LocalAllocator;
		}

		constexpr ETaskNodeType GetNodeType() const
		{
			return m_NodeType;
		}
	private:
		castl::string m_Name = "";
		ETaskNodeType m_NodeType;
		int32_t m_ExplicitDepsCount = 0;
		int32_t m_DepsCount = 0;
		TaskQueueManager* m_TaskQueue = nullptr;
		castl::vector<TaskNodeBase*> m_Successors;
		NodeLocalTaskAllocator m_LocalAllocator;
	};

	class TaskNodeGeneral_Impl final : public TaskNodeBase_Impl, public TaskNodeGeneral
	{
	public:
		TaskNodeGeneral_Impl(RootTaskAllocator* rootAllocator) : TaskNodeBase_Impl(ETaskNodeType::eGeneral, rootAllocator)
		{
		}
		virtual TaskNodeGeneral* Func(castl::function<void()>& functor) override
		{
			m_Functor = functor;
			return this;
		}
		virtual TaskNodeGeneral* Func(castl::function<void(TaskScheduler*)>& functor) override
		{
			m_FunctorWithScheduler = functor;
			return this;
		}
		virtual void Execute() override
		{
			if (m_FunctorWithScheduler)
			{
				m_FunctorWithScheduler(nullptr);
			}
			else if (m_Functor)
			{
				m_Functor();
			}
		}

	private:
		castl::function<void()> m_Functor;
		castl::function<void(TaskScheduler*)> m_FunctorWithScheduler;
	};

	class RootTaskAllocator : public TaskAllocator
	{
	public:
		virtual TaskNodeGeneral_Impl* NewGeneralTaskNode() override
		{
			return new TaskNodeGeneral_Impl(this);
		}
		virtual void ReleaseTaskNode(TaskNodeBase_Impl* node) override
		{
			switch (node->GetNodeType())
			{
			case ETaskNodeType::eGeneral:
				delete static_cast<TaskNodeGeneral_Impl*>(node);
				break;
			case ETaskNodeType::eParallel:
				break;
			}
		}
	};


	class TaskManager : public TaskObjectBase
	{
	public:
		void Loop(castl::function<void(TaskScheduler*)> loopFunctor)
		{

		}
	private:
		class TaskScheduler_Impl : public TaskScheduler
		{
			TaskQueueManager* m_TaskQueue;
			TaskAllocator* m_Allocator;

			virtual TaskNodeGeneral* NewTaskNode() override
			{
				return m_Allocator->NewGeneralTaskNode();
			}

			virtual TaskNodeParallel* NewTaskNodeParallel() override
			{
				return nullptr;
			}

			virtual void ExecuteAndWait(castl::array_ref<TaskNodeBase*> nodes) override
			{
				if(nodes.empty())
				{
					return;
				}

				for (TaskNodeBase* taskNode : nodes)
				{
					TaskNodeBase_Impl* taskNodeImpl = dynamic_cast<TaskNodeBase_Impl*>(taskNode);
					if (taskNodeImpl->ReadyToExecute())
					{

					}
				}
			}
		};
		TaskQueueManager m_TaskQueue;
		RootTaskAllocator m_RootAllocator;
	};
}