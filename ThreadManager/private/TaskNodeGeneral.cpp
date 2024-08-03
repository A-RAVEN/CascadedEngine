#include "TaskNodeGeneral.h"
#include <CASTL/CAVector.h>
#include <CASTL/CAMutex.h>

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
		void EnqueueSingleTaskNode(TaskNodeBase_Impl* node)
		{
			castl::lock_guard<castl::mutex> lock(m_Mutex);
			EnqueueSingleTaskNodeNoLock(node);
		}
		void ExecuteTaskNodes(castl::array_ref<TaskNodeBase*> nodes, bool wait)
		{
			castl::lock_guard<castl::mutex> lock(m_Mutex);
			//TODO: Insert nodes
			for (TaskNodeBase* taskNode : nodes)
			{
				TaskNodeBase_Impl* taskNodeImpl = dynamic_cast<TaskNodeBase_Impl*>(taskNode);
				if (taskNodeImpl->ReadyToExecute())
				{
					EnqueueSingleTaskNodeNoLock(taskNodeImpl);
				}
			}
			if (wait)
			{
				//TODO: Goto Thread Wait Function
			}
		}
	private:
		void EnqueueSingleTaskNodeNoLock(TaskNodeBase_Impl* node)
		{
			//m_TaskNodes.push_back(node);
		}
		castl::mutex m_Mutex;
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

		void SetTaskQueue(TaskQueue* taskQueue)
		{
			m_TaskQueue = taskQueue;
		}

		TaskQueue* GetTaskQueue()
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
		TaskQueue* m_TaskQueue = nullptr;
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
			TaskQueue* m_TaskQueue;
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
		TaskQueue m_TaskQueue;
		RootTaskAllocator m_RootAllocator;
	};
}