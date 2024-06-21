#include "pch.h"
#include "ThreadManager_Impl.h"
#include <DebugUtils.h>

namespace thread_management
{
    CTaskGraph* TaskGraph_Impl1::Name(castl::string name)
    {
        Name_Internal(name);
        return this;
    }

    CTaskGraph* TaskGraph_Impl1::DependsOn(CTask* parentTask)
    {
        CTask_Impl1* task = static_cast<CTask_Impl1*>(parentTask);
        DependsOn_Internal(task);
        return this;
    }

    CTaskGraph* TaskGraph_Impl1::DependsOn(TaskParallelFor* parentTask)
    {
        TaskParallelFor_Impl* task = static_cast<TaskParallelFor_Impl*>(parentTask);
        DependsOn_Internal(task);
        return this;
    }

    CTaskGraph* TaskGraph_Impl1::DependsOn(CTaskGraph* parentTask)
    {
        TaskGraph_Impl1* task = static_cast<TaskGraph_Impl1*>(parentTask);
        DependsOn_Internal(task);
        return this;
    }

    CTaskGraph* TaskGraph_Impl1::WaitOnEvent(castl::string const& name)
    {
        WaitEvent_Internal(name);
        return this;
    }

    CTaskGraph* TaskGraph_Impl1::SignalEvent(castl::string const& name)
    {
        SignalEvent_Internal(name);
        return this;
    }

    CTaskGraph* TaskGraph_Impl1::SetupFunctor(std::function<void(CTaskGraph* thisGraph)> functor)
    {
        m_Functor = functor;
        return this;
    }

    CTaskGraph* TaskGraph_Impl1::ForceRunOnMainThread()
    {
        m_RunOnMainThread = true;
        return this;
    }

    CTaskGraph* TaskGraph_Impl1::Thread(cacore::HashObj<castl::string> const& threadKey)
    {
        SetThreadKey_Internal(threadKey);
        return this;
    }

    void TaskGraph_Impl1::AddResource(castl::shared_ptr<void> const& resource)
    {
        AddResource_Internal(resource);
    }

    std::shared_future<void> TaskGraph_Impl1::Run()
    {
        return StartExecute();
    }

    CTask* TaskGraph_Impl1::NewTask()
    {
        std::lock_guard<std::mutex> guard(m_Mutex);
        auto result = m_Allocator->NewTask(this);
        result->Thread(m_ThreadKey);
        result->SetRunOnMainThread(m_RunOnMainThread);
        m_SubTasks.push_back(result);
        return result;
    }

    TaskParallelFor* TaskGraph_Impl1::NewTaskParallelFor()
    {
        std::lock_guard<std::mutex> guard(m_Mutex);
        auto result = m_Allocator->NewTaskParallelFor(this);
        result->SetRunOnMainThread(m_RunOnMainThread);
        m_SubTasks.push_back(result);
        return result;
    }

    CTaskGraph* TaskGraph_Impl1::NewTaskGraph()
    {
        std::lock_guard<std::mutex> guard(m_Mutex);
        auto result = m_Allocator->NewTaskGraph(this);
        result->Thread(m_ThreadKey);
        result->SetRunOnMainThread(m_RunOnMainThread);
        m_SubTasks.push_back(result);
        return result;
    }

    TaskGraph_Impl1::TaskGraph_Impl1(TaskBaseObject* owner, ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator) :
        TaskNode(TaskObjectType::eGraph, owner, owningManager, allocator)
    {
    }

    void TaskGraph_Impl1::Release()
    {
        m_PendingSubnodeCount.store(0, std::memory_order_relaxed);
        m_RootTasks.clear();
        m_SubTasks.clear();
        m_Functor = nullptr;
        Release_Internal();
    }

    void TaskGraph_Impl1::NotifyChildNodeFinish(TaskNode* childNode)
    {
        uint32_t remainCounter = --m_PendingSubnodeCount;
        if (remainCounter == 0)
        {
            FinalizeExecution_Internal();
        }
    }
    void TaskGraph_Impl1::Execute_Internal()
    {
        if (m_Functor != nullptr)
        {
            m_Functor(this);
        }
        if (m_SubTasks.empty())
        {
            FinalizeExecution_Internal();
            return;
        }
        SetupSubnodeDependencies();
        m_OwningManager->EnqueueTaskNodes_Loop(m_RootTasks);
    }

    void TaskGraph_Impl1::SetupSubnodeDependencies()
    {
        m_RootTasks.clear();
        for (TaskNode* itrTask : m_SubTasks)
        {
            itrTask->SetupThisNodeDependencies_Internal();
            if (itrTask->GetDepenedentCount() == 0)
            {
                m_RootTasks.push_back(itrTask);
            }
        }
        uint32_t pendingTaskCount = m_SubTasks.size();
        m_PendingSubnodeCount.store(pendingTaskCount, std::memory_order_release);
    }
    CTask* CTask_Impl1::ForceRunOnMainThread()
    {
        m_RunOnMainThread = true;
        return this;
    }
    CTask* CTask_Impl1::Thread(cacore::HashObj<castl::string> const& threadKey)
    {
        SetThreadKey_Internal(threadKey);
        return this;
    }
    CTask* CTask_Impl1::Name(castl::string name)
    {
        Name_Internal(name);
        return this;
    }
    CTask* CTask_Impl1::DependsOn(CTask* parentTask)
    {
        CTask_Impl1* task = static_cast<CTask_Impl1*>(parentTask);
        DependsOn_Internal(task);
        return this;
    }
    CTask* CTask_Impl1::DependsOn(TaskParallelFor* parentTask)
    {
        TaskParallelFor_Impl* task = static_cast<TaskParallelFor_Impl*>(parentTask);
        DependsOn_Internal(task);
        return this;
    }
    CTask* CTask_Impl1::DependsOn(CTaskGraph* parentTask)
    {
        TaskGraph_Impl1* task = static_cast<TaskGraph_Impl1*>(parentTask);
        DependsOn_Internal(task);
        return this;
    }
    CTask* CTask_Impl1::WaitOnEvent(castl::string const& name)
    {
        WaitEvent_Internal(name);
        return this;
    }
    CTask* CTask_Impl1::SignalEvent(castl::string const& name)
    {
        SignalEvent_Internal(name);
        return this;
    }
    std::shared_future<void> CTask_Impl1::Run()
    {
        return StartExecute();
    }
    CTask* CTask_Impl1::Functor(std::function<void()>&& functor)
    {
        Functor_Internal(std::move(functor));
        return this;
    }
    CTask_Impl1::CTask_Impl1(TaskBaseObject* owner, ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator) :
        TaskNode(TaskObjectType::eNode, owner, owningManager, allocator)
    {
    }

    void CTask_Impl1::Functor_Internal(std::function<void()>&& functor)
    {
        m_Functor = functor;
    }

    void CTask_Impl1::Release()
    {
        m_RunOnMainThread = false;
        m_Functor = nullptr;
        Release_Internal();
    }

    void CTask_Impl1::Execute_Internal()
    {
        //CA_LOG_ERR("Execute " + m_Name);
        if (m_Functor != nullptr)
        {
            m_Functor();
        }
        //CA_LOG_ERR("Finalize " + m_Name);
        FinalizeExecution_Internal();
    }

    ThreadManager_Impl1::ThreadManager_Impl1() : 
        TaskBaseObject(TaskObjectType::eManager)
        , m_TaskNodeAllocator(this)
    {
    }

    ThreadManager_Impl1::~ThreadManager_Impl1()
    {
        Stop();
        for (std::thread& itrThread : m_WorkerThreads)
        {
            itrThread.join();
        }
    }

    void ThreadManager_Impl1::EnqueueSetupTask()
    {
        if (m_PrepareFunctor == nullptr)
        {
            Stop();
            return;
        }
        TaskGraph_Impl1* setupTaskGraph = m_TaskNodeAllocator.NewTaskGraph(this);
        setupTaskGraph->Name("Setup");
        setupTaskGraph->SignalEvent(m_SetupEventName);
        for (auto initializeTask : m_InitializeTasks)
        {
            setupTaskGraph->DependsOn_Internal(initializeTask);
        }
        bool notEnd = m_PrepareFunctor(setupTaskGraph);
        ++m_Frames;
        EnqueueTaskNodes_Loop(m_InitializeTasks);
        m_InitializeTasks.clear();
        EnqueueTaskNode(setupTaskGraph);
        if (!notEnd)
        {
            Stop();
        }
    }

    void ThreadManager_Impl1::InitializeThreadCount(uint32_t threadNum, uint32_t dedicateThreadNum)
    {
        m_WorkerThreads.reserve(threadNum + dedicateThreadNum);
        uint32_t dedicateTaskQueueNum = dedicateThreadNum + 1;
        m_DedicateTaskQueues.resize(dedicateTaskQueueNum);
        m_DedicateThreadMap.SetThreadIndex(castl::string{ "MainThread" }, 0);
        for (uint32_t i = 0; i < threadNum; ++i)
        {
            m_WorkerThreads.emplace_back(&ThreadManager_Impl1::ProcessingWorks, this, i);
        }
        for (uint32_t i = 0; i < dedicateThreadNum; ++i)
        {
			m_WorkerThreads.emplace_back(&DedicateTaskQueue::WorkLoop, &m_DedicateTaskQueues[i + 1]);
		}
    }
    void ThreadManager_Impl1::SetDedicateThreadMapping(uint32_t dedicateThreadIndex, cacore::HashObj<castl::string> const& name)
    {
        m_DedicateThreadMap.SetThreadIndex(name, dedicateThreadIndex);
    }
    CTask* ThreadManager_Impl1::NewTask()
    {
        return m_TaskNodeAllocator.NewTask(this);
    }
    TaskParallelFor* ThreadManager_Impl1::NewTaskParallelFor()
    {
        return m_TaskNodeAllocator.NewTaskParallelFor(this);
    }
    CTaskGraph* ThreadManager_Impl1::NewTaskGraph()
    {
        return m_TaskNodeAllocator.NewTaskGraph(this);
    }

    void ThreadManager_Impl1::LogStatus() const
    {
        m_TaskNodeAllocator.LogStatus();
    }

    void ThreadManager_Impl1::OneTime(std::function<void(CTaskGraph*)> functor, castl::string const& waitingEvent)
    {
        if (functor == nullptr)
            return;
        auto* newTaskGraph = m_TaskNodeAllocator.NewTaskGraph(this);
        functor(newTaskGraph);
        std::lock_guard<std::mutex> guard(m_Mutex);
        m_InitializeTasks.push_back(newTaskGraph);
    }

    void ThreadManager_Impl1::LoopFunction(std::function<bool(CTaskGraph*)> functor, castl::string const& waitingEvent)
    {
        m_PrepareFunctor = functor;
        m_SetupEventName = waitingEvent;
    }

    void ThreadManager_Impl1::Run()
    {
        EnqueueSetupTask();
        uint32_t mainThreadID = m_WorkerThreads.size();
        ProcessingWorksMainThread();
        for (std::thread& itrThread : m_WorkerThreads)
        {
            itrThread.join();
        }
    }

    void ThreadManager_Impl1::Stop()
    {
        {
            for (auto& dedicateThread : m_DedicateTaskQueues)
            {
                dedicateThread.Stop();
            }
            m_Stopped = true;
            m_ConditinalVariable.notify_all();
        }

    }

    void ThreadManager_Impl1::EnqueueTaskNode(TaskNode* enqueueNode)
    {
        if(enqueueNode->m_ThreadKey.Valid())
        {
			EnqueueTaskNode_DedicateThread(enqueueNode);
		}
		else
		{
            EnqueueTaskNode_GeneralThread(enqueueNode);
		}
    }
    
    void ThreadManager_Impl1::EnqueueTaskNodes_Loop(castl::array_ref<TaskNode*> nodes)
    {
        for (TaskNode* node : nodes)
        {
            EnqueueTaskNode(node);
        }
    }

    void ThreadManager_Impl1::EnqueueTaskNodes_GeneralThread(castl::array_ref<TaskNode*> nodes)
    {
        for (TaskNode* itrNode : nodes)
        {
            itrNode->m_Running.store(true, std::memory_order_relaxed);
        }
        {
            std::lock_guard<std::mutex> guard(m_Mutex);
            for (TaskNode* itrNode : nodes)
            {
                m_TaskQueue.push_back(itrNode);
            }
            if (nodes.size() > 1)
            {
                m_ConditinalVariable.notify_all();
            }
            else
            {
                m_ConditinalVariable.notify_one();
            }
        }
    }
    void ThreadManager_Impl1::EnqueueTaskNode_GeneralThread(TaskNode* node)
    {
        if (m_EventManager.WaitEventDone(node))
        {
            node->m_Running.store(true, std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> guard(m_Mutex);
                m_TaskQueue.push_back(node);
                m_ConditinalVariable.notify_one();
            }
        }
    }
    void ThreadManager_Impl1::EnqueueTaskNode_DedicateThread(TaskNode* node)
    {
        if (m_EventManager.WaitEventDone(node))
        {
            node->m_Running.store(true, std::memory_order_relaxed);
            uint32_t dedicateThreadID = m_DedicateThreadMap.GetThreadIndex(node->m_ThreadKey) % m_DedicateTaskQueues.size();
            m_DedicateTaskQueues[dedicateThreadID].EnqueueTaskNodes(node);
        }
    }
    
    void ThreadManager_Impl1::SignalEvent(castl::string const& eventName, uint64_t signalFrame)
    {
        if (eventName == m_SetupEventName)
        {
            EnqueueSetupTask();
        }
        m_EventManager.SignalEvent(*this, eventName, signalFrame);
    }
    
    void ThreadManager_Impl1::NotifyChildNodeFinish(TaskNode* childNode)
    {

    }
    void ThreadManager_Impl1::ProcessingWorks()
    {
        while (!m_Stopped)
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_ConditinalVariable.wait(lock, [this]()
                {
                    //TaskQueue不是空的，或者线程管理器已经停止，不再等待
                    return m_Stopped || !m_TaskQueue.empty();
                });

            if (m_TaskQueue.empty())
            {
                lock.unlock();
                continue;
            }
            if (m_Stopped)
            {
                lock.unlock();
                return;
            }
            auto task = m_TaskQueue.front();
            m_TaskQueue.pop_front();
            lock.unlock();
            task->Execute_Internal();
        }
    }

    void ThreadManager_Impl1::ProcessingWorksMainThread()
    {
        m_DedicateTaskQueues[0].WorkLoop();
    }

    CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(CThreadManager, ThreadManager_Impl1)


    TaskParallelFor* TaskParallelFor_Impl::Name(castl::string name)
    {
        Name_Internal(name);
        return this;
    }

    TaskParallelFor* TaskParallelFor_Impl::DependsOn(CTask* parentTask)
    {
        CTask_Impl1* task = static_cast<CTask_Impl1*>(parentTask);
        DependsOn_Internal(task);
        return this;
    }

    TaskParallelFor* TaskParallelFor_Impl::DependsOn(TaskParallelFor* parentTask)
    {
        TaskParallelFor_Impl* task = static_cast<TaskParallelFor_Impl*>(parentTask);
        DependsOn_Internal(task);
        return this;
    }

    TaskParallelFor* TaskParallelFor_Impl::DependsOn(CTaskGraph* parentTask)
    {
        TaskGraph_Impl1* task = static_cast<TaskGraph_Impl1*>(parentTask);
        DependsOn_Internal(task);
        return this;
    }

    TaskParallelFor* TaskParallelFor_Impl::WaitOnEvent(castl::string const& name)
    {
        WaitEvent_Internal(name);
        return this;
    }

    TaskParallelFor* TaskParallelFor_Impl::SignalEvent(castl::string const& name)
    {
        SignalEvent_Internal(name);
        return this;
    }

    TaskParallelFor* TaskParallelFor_Impl::Functor(std::function<void(uint32_t)> functor)
    {
        m_Functor = functor;
        return this;
    }

    TaskParallelFor* TaskParallelFor_Impl::JobCount(uint32_t jobCount)
    {
        m_PendingSubnodeCount.store(jobCount, std::memory_order_release);
        return this;
    }

    std::shared_future<void> TaskParallelFor_Impl::Run()
    {
        return StartExecute();
    }

    TaskParallelFor_Impl::TaskParallelFor_Impl(TaskBaseObject* owner, ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator) :
        TaskNode(TaskObjectType::eNodeParallel, owner, owningManager, allocator)
    {
    }

    void TaskParallelFor_Impl::Release()
    {
        m_PendingSubnodeCount.store(0, std::memory_order_relaxed);
        m_TaskList.clear();
        Release_Internal();
    }

    void TaskParallelFor_Impl::NotifyChildNodeFinish(TaskNode* childNode)
    {
        uint32_t remainCounter = --m_PendingSubnodeCount;
        if (remainCounter == 0)
        {
            FinalizeExecution_Internal();
        }
    }

    void TaskParallelFor_Impl::Execute_Internal()
    {
        if (m_PendingSubnodeCount == 0 || m_Functor == nullptr)
        {
            FinalizeExecution_Internal();
            return;
        };
        m_TaskList.clear();
        m_TaskList.reserve(m_PendingSubnodeCount);
        for (uint32_t taskId = 0; taskId < m_PendingSubnodeCount; ++taskId)
        {
            auto pTask = m_Allocator->NewTask(this);
            pTask->SetRunOnMainThread(m_RunOnMainThread);
            pTask->Name(m_Name + " Subtask:" + castl::to_string(taskId))
                ->Functor([functor = m_Functor, taskId]()
                    {
                        functor(taskId);
                    });
            m_TaskList.push_back(pTask);
        };
        m_OwningManager->EnqueueTaskNodes_GeneralThread(m_TaskList);
    }
    TaskNodeAllocator::TaskNodeAllocator(ThreadManager_Impl1* owningManager) :  
        m_OwningManager(owningManager)
        , m_TaskGraphPool()
        , m_TaskPool()
        , m_TaskParallelForPool()
    {
    }
    CTask_Impl1* TaskNodeAllocator::NewTask(TaskBaseObject* owner)
    {
        auto result = m_TaskPool.Alloc(owner, m_OwningManager, this);
        result->SetOwner(owner);
        return result;
    }
    TaskParallelFor_Impl* TaskNodeAllocator::NewTaskParallelFor(TaskBaseObject* owner)
    {
        auto result = m_TaskParallelForPool.Alloc(owner, m_OwningManager, this);
        result->SetOwner(owner);
        return result;
    }
    TaskGraph_Impl1* TaskNodeAllocator::NewTaskGraph(TaskBaseObject* owner)
    {
        auto result = m_TaskGraphPool.Alloc(owner, m_OwningManager, this);
        result->SetOwner(owner);
        return result;
    }
   
    void TaskNodeAllocator::Release(TaskNode* childNode)
    {
        switch (childNode->GetTaskObjectType())
        {
        case TaskObjectType::eGraph:
        {
            m_TaskGraphPool.Release(static_cast<TaskGraph_Impl1*>(childNode));
            break;
        }
        case TaskObjectType::eNode:
        {
            m_TaskPool.Release(static_cast<CTask_Impl1*>(childNode));
            break;
        }
        case TaskObjectType::eNodeParallel:
        {
            m_TaskParallelForPool.Release(static_cast<TaskParallelFor_Impl*>(childNode));
            break;
        }
        default:
            CA_LOG_ERR("Invalid TaskNode Type");
            break;
        }
    }

    void TaskNodeAllocator::LogStatus() const
    {
        std::cout << "tasks: " << m_TaskGraphPool.GetPoolSize() << ";  " << m_TaskGraphPool.GetEmptySpaceSize() << std::endl;
        std::cout << "parallelTasks: " << m_TaskParallelForPool.GetPoolSize() << ";  " << m_TaskParallelForPool.GetEmptySpaceSize() << std::endl;
        std::cout << "taskGraphs: " << m_TaskGraphPool.GetPoolSize() << ";  " << m_TaskGraphPool.GetEmptySpaceSize() << std::endl;
    }
    //void ThreadManager_Impl1::TaskWaitList::Signal(uint64_t signalFrame)
    //{
    //    if (signalFrame > m_SignaledFrame)
    //    {
    //        m_SignaledFrame = signalFrame;
    //    }
    //}

    void DedicateTaskQueue::Stop()
    {
        std::lock_guard<std::mutex> guard(m_Mutex);
        m_Stop = true;
    }
    void DedicateTaskQueue::WorkLoop()
    {
        while (!m_Stop)
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_ConditionalVariable.wait(lock, [this]()
                {
                    return m_Stop || !m_Queue.empty();
                });
            if (m_Queue.empty() || m_Stop)
            {
                lock.unlock();
                continue;
            }
            auto task = m_Queue.front();
            m_Queue.pop_front();
            lock.unlock();
            task->Execute_Internal();
        }
    }
    void DedicateTaskQueue::EnqueueTaskNodes(castl::array_ref<TaskNode*> const& nodeDeque)
    {
        {
            std::lock_guard<std::mutex> guard(m_Mutex);
            EnqueueTaskNodes_NoLock(nodeDeque);
        }
        m_ConditionalVariable.notify_one();
    }
    void DedicateTaskQueue::EnqueueTaskNodes_NoLock(castl::array_ref<TaskNode*> const& nodeDeque)
    {
        for (TaskNode* itrNode : nodeDeque)
        {
            m_Queue.push_back(itrNode);
        }
    }
    void TaskNodeEventManager::SignalEvent(ThreadManager_Impl1& threadManager, cacore::HashObj<castl::string> const& eventKey, uint64_t signalFrame)
    {
        castl::lock_guard<castl::mutex> guard(m_Mutex);
        auto found = m_EventMap.find(eventKey);
        if (found == m_EventMap.end())
        {
            found = m_EventMap.insert(castl::make_pair(eventKey, m_EventWaitLists.size())).first;
            m_EventWaitLists.emplace_back();
        }
        auto& waitList = m_EventWaitLists[found->second];
        waitList.Signal(signalFrame);
        if (!waitList.m_WaitingFrames.empty())
        {
            while (!waitList.m_WaitingFrames.empty() && waitList.m_WaitingFrames.front().first <= waitList.m_SignaledFrame)
            {
                auto waitCount = waitList.m_WaitingFrames.front().second;
                waitList.m_WaitingFrames.pop_front();
                for (uint32_t i = 0; i < waitCount; ++i)
                {
                    TaskNode* waitNode = waitList.m_WaitingTasks.front();
                    waitList.m_WaitingTasks.pop_front();
                    CA_ASSERT(!waitNode->m_Running, "Task is already running");
                    threadManager.EnqueueTaskNode(waitNode);
                }
            }
        }
    }
    bool TaskNodeEventManager::WaitEventDone(TaskNode* node)
    {
        if (!node->m_EventName.empty())
        {
            castl::lock_guard<castl::mutex> guard(m_Mutex);
            auto found = m_EventMap.find(node->m_EventName);
            if (found != m_EventMap.end())
            {
                auto& waitList = m_EventWaitLists[found->second];
                if (waitList.m_SignaledFrame < node->m_CurrentFrame)
                {
                    waitList.m_WaitingTasks.push_back(node);
                    if (waitList.m_WaitingFrames.empty() || waitList.m_WaitingFrames.back().first < node->m_CurrentFrame)
                    {
                        waitList.m_WaitingFrames.push_back(castl::make_pair(node->m_CurrentFrame, 1));
                    }
                    else
                    {
                        waitList.m_WaitingFrames.back().second++;
                    }
                    return false;
                }
            }
        }
        return true;
    }
}


