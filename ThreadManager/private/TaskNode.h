#pragma once
#include <CASTL/CAVector.h>
#include <CASTL/CAString.h>
#include <CASTL/CASharedPtr.h>
#include <Hasher.h>
namespace thread_management
{
	class ThreadManager_Impl1;
	class TaskNodeAllocator;
	class TaskNode;
	enum class TaskObjectType
	{
		eManager = 0,
		eGraph,
		eNode,
		eNodeParallel,
		eTaskScheduler,
	};

	enum class TaskNodeState : uint8_t
	{
		eInvalid = 0,
		ePrepare,
		ePending,
	};

	class TaskBaseObject
	{
	public:
		TaskBaseObject(TaskObjectType type) :m_Type(type){}
		virtual void NotifyChildNodeFinish(TaskNode* childNode) {}
		virtual uint64_t GetCurrentFrame() const = 0;
		TaskObjectType GetTaskObjectType() const { return m_Type; }
	private:
		TaskObjectType m_Type;
	};

	class TaskNode : public TaskBaseObject
	{
	public:
		TaskNode(TaskObjectType type, ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator);
		void SetOwner(TaskBaseObject* owner) { 
			m_Owner = owner;
			m_CurrentFrame = owner->GetCurrentFrame(); }
		bool Valid() const { return m_Owner != nullptr; }
		virtual bool RunOnMainThread() const { return m_RunOnMainThread; }
		virtual void NotifyChildNodeFinish(TaskNode* childNode) override {}
		virtual void Execute_Internal() = 0;
		virtual uint64_t GetCurrentFrame() const override { return m_CurrentFrame; }
		void SetRunOnMainThread(bool runOnMainThread) { m_RunOnMainThread = runOnMainThread; }
		void SetupThisNodeDependencies_Internal();
		size_t GetDepenedentCount() const { return m_Dependents.size(); }
		void ReleaseSelf();
		void Initialize_Internal() { m_Running.store(TaskNodeState::ePrepare); m_Name = "Default Task Name"; }
		void Release_Internal();
		void SetThreadKey_Internal(cacore::HashObj<castl::string> const& key) { m_ThreadKey = key; }
		castl::string const& GetName() const { return m_Name; }
		bool WaitingToRun(TaskBaseObject* owner) const
		{
			if (owner != m_Owner)
				return false;
			if (m_Running.load() != TaskNodeState::ePrepare)
				return false;
			return true;
		}
	protected:
		void NotifyDependsOnFinish(TaskNode* dependsOnNode);
		void Name_Internal(const castl::string& name);
		void WaitEvent_Internal(const castl::string& name);
		void SignalEvent_Internal(const castl::string& name);
		void DependsOn_Internal(TaskNode* dependsOnNode);
		void FinalizeExecution_Internal();
	protected:
		ThreadManager_Impl1* m_OwningManager;
		TaskNodeAllocator* m_Allocator;
		castl::atomic<TaskBaseObject*> m_Owner{ nullptr };
		castl::atomic<TaskNodeState> m_Running{ TaskNodeState::eInvalid };
		cacore::HashObj<castl::string> m_ThreadKey;
		castl::string m_Name = "Default Task Name";
		castl::string m_EventName;
		castl::string m_SignalEventName;
		uint64_t m_CurrentFrame;
		castl::vector<TaskNode*>m_Dependents;
		castl::vector<TaskNode*>m_Successors;
		castl::atomic<uint32_t>m_PendingDependsOnTaskCount{0};
		bool m_RunOnMainThread = false;

		friend class TaskNodeAllocator;
		friend class ThreadManager_Impl1;
		friend class TaskNodeEventManager;
		friend class TaskScheduler_Impl;
	};
}


