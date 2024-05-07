#pragma once
#include <future>
#include <CASTL/CAVector.h>
#include <CASTL/CAString.h>
#include <CASTL/CASharedPtr.h>
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
		TaskNode(TaskObjectType type, TaskBaseObject* owner, ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator);
		void SetOwner(TaskBaseObject* owner) { m_Owner = owner; }
		std::shared_future<void> StartExecute();
		virtual bool RunOnMainThread() const { return false; }
		virtual void NotifyChildNodeFinish(TaskNode* childNode) override {}
		virtual void Execute_Internal() = 0;
		virtual void SetupSubnodeDependencies() {};
		virtual uint64_t GetCurrentFrame() const override { return m_CurrentFrame; }
		void SetupThisNodeDependencies_Internal();
		size_t GetDepenedentCount() const { return m_Dependents.size(); }
		void Release_Internal();
		std::shared_future<void> AquireFuture();
		void FulfillPromise();
	protected:
		void NotifyDependsOnFinish(TaskNode* dependsOnNode);
		void Name_Internal(const castl::string& name);
		void WaitEvent_Internal(const castl::string& name);
		void SignalEvent_Internal(const castl::string& name);
		void DependsOn_Internal(TaskNode* dependsOnNode);
		void FinalizeExecution_Internal();
		void AddResource_Internal(castl::shared_ptr<void> const& resource);
	protected:
		ThreadManager_Impl1* m_OwningManager;
		TaskBaseObject* m_Owner;
		TaskNodeAllocator* m_Allocator;
		std::atomic_bool m_Running{ false };
		castl::string m_Name;
		castl::string m_EventName;
		castl::string m_SignalEventName;
		uint64_t m_CurrentFrame;
		castl::vector<TaskNode*>m_Dependents;
		castl::vector<TaskNode*>m_Successors;
		std::atomic<uint32_t>m_PendingDependsOnTaskCount{0};
		castl::vector<castl::shared_ptr<void>> m_BoundResources;

		bool m_HasPromise = false;
		std::promise<void> m_Promise;

		friend class ThreadManager_Impl1;
	};
}


