#pragma once
#include <CASTL/CAVector.h>
#include <CASTL/CAString.h>
#include <CASTL/CASharedPtr.h>
#include <Hasher.h>
namespace thread_management
{
	class ThreadManager_Impl;
	class TaskNodeAllocator;
	class TaskNode;

	enum class TaskNodeState : uint8_t
	{
		eInvalid = 0,
		ePrepare,
		ePending,
	};

	class TaskBaseObject
	{
	public:
		virtual void NotifyChildNodeFinish(TaskNode* childNode) {}
		virtual uint64_t GetCurrentFrame() const = 0;
	private:
	};

	class TaskNode : public TaskBaseObject
	{
	public:
		TaskNode(ThreadManager_Impl* owningManager, TaskNodeAllocator* allocator);
		void SetOwner(TaskBaseObject* owner) { 
			m_Owner = owner;
			m_CurrentFrame = owner->GetCurrentFrame(); }
		bool Valid() const { return m_Owner != nullptr; }
		virtual bool RunOnMainThread() const;
		virtual void NotifyChildNodeFinish(TaskNode* childNode) override {}
		virtual void Execute_Internal() = 0;
		virtual uint64_t GetCurrentFrame() const override { return m_CurrentFrame; }
		void SetupThisNodeDependencies_Internal();
		size_t GetDepenedentCount() const { return m_Dependents.size(); }
		void ReleaseSelf();
		void Initialize_Internal() { m_Running.store(TaskNodeState::ePrepare); }
		void Release_Internal();
		void SetThreadKey_Internal(cacore::NameHash const& key) { m_ThreadKey = key; }
		castl::string const& GetName() const { return castl::string(m_Name.Get()); }
		TaskNodeState GetState() const { return m_Running.load(); }
		bool WaitingToRun(TaskBaseObject* owner) const
		{
			//CHECK Function
			if (owner != m_Owner)
				return false;
			if (m_Running.load() != TaskNodeState::ePrepare)
				return false;
			return true;
		}
	protected:
		void NotifyDependsOnFinish(TaskNode* dependsOnNode);
		void Name_Internal(const cacore::NameHash& name);
		void WaitEvent_Internal(const cacore::NameHash& name);
		void SignalEvent_Internal(const cacore::NameHash& name);
		void DependsOn_Internal(TaskNode* dependsOnNode);
		void FinalizeExecution_Internal();
	protected:
		ThreadManager_Impl* m_OwningManager;
		TaskNodeAllocator* m_Allocator;
		castl::atomic<TaskBaseObject*> m_Owner{ nullptr };
		castl::atomic<TaskNodeState> m_Running{ TaskNodeState::eInvalid };
		uint64_t m_CurrentFrame;

		cacore::NameHash m_ThreadKey;
		cacore::NameHash m_Name;
		cacore::NameHash m_EventName;
		cacore::NameHash m_SignalEventName;

		castl::vector<TaskNode*>m_Dependents;
		castl::vector<TaskNode*>m_Successors;
		castl::atomic<uint32_t>m_PendingDependsOnTaskCount{0};

		friend class TaskNodeAllocator;
		friend class ThreadManager_Impl;
		friend class TaskNodeEventManager;
		friend class TaskScheduler_Impl;
	};
}


