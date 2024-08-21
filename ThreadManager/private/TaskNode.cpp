#include "pch.h"
#include "TaskNode.h"
#include <CACore/header/DebugUtils.h>
#include "ThreadManager_Impl.h"

namespace thread_management
{
	TaskNode::TaskNode(ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator)
		: m_OwningManager(owningManager)
		, m_Allocator(allocator)
	{
	}
	void TaskNode::NotifyDependsOnFinish(TaskNode* dependsOnNode)
	{
		uint32_t remainCounter = --m_PendingDependsOnTaskCount;
		if (remainCounter == 0)
		{
			m_OwningManager->EnqueueTaskNode(this);
		}
	}
	void TaskNode::Name_Internal(const castl::string& name)
	{
		m_Name = name;
	}
	void TaskNode::WaitEvent_Internal(const castl::string& name)
	{
		m_EventName = name;
	}
	void TaskNode::SignalEvent_Internal(const castl::string& name)
	{
		m_SignalEventName = name;
	}
	void TaskNode::DependsOn_Internal(TaskNode* dependsOnNode)
	{
		CA_ASSERT(dependsOnNode->m_Owner == m_Owner, "Dependency Is Valid For Nodes Under Same Owner");
		dependsOnNode->m_Successors.push_back(this);
		m_Dependents.push_back(dependsOnNode);
	}
	void TaskNode::SetupThisNodeDependencies_Internal()
	{
		uint32_t pendingCount = m_Dependents.size();
		m_PendingDependsOnTaskCount.store(pendingCount, castl::memory_order_release);
	}
	void TaskNode::ReleaseSelf()
	{
		m_Allocator->Release(dynamic_cast<TaskBase*>(this));
	}
	void TaskNode::Release_Internal()
	{
		m_Owner = nullptr;
		m_Running.store(TaskNodeState::eInvalid, castl::memory_order_release);
		m_PendingDependsOnTaskCount.store(0, castl::memory_order_release);
		m_Name = "Default Task Name";
		m_EventName = "";
		m_SignalEventName = "";
		m_CurrentFrame = 0;
		m_Dependents.clear();
		m_Successors.clear();
		m_RunOnMainThread = false;
		m_ThreadKey = {};
	}
	void TaskNode::FinalizeExecution_Internal()
	{
		for (auto itrSuccessor = m_Successors.begin(); itrSuccessor != m_Successors.end(); ++itrSuccessor)
		{
			(*itrSuccessor)->NotifyDependsOnFinish(this);
		}
		if (!m_SignalEventName.empty())
		{
			m_OwningManager->SignalEvent(m_SignalEventName, m_CurrentFrame);
		}
		m_Owner.load()->NotifyChildNodeFinish(this);
	}
}

