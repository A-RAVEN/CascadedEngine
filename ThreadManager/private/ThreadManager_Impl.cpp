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
            //CA_LOG_ERR("Finalize " + m_Name);
            FinalizeExecution_Internal();
        }
    }
    void TaskGraph_Impl1::Execute_Internal()
    {
        //CA_LOG_ERR("Execute " + m_Name);
        if (m_Functor != nullptr)
        {
            m_Functor(this);
        }
        if (m_SubTasks.empty())
        {
            //CA_LOG_ERR("Finalize " + m_Name);
            FinalizeExecution_Internal();
            return;
        }
        SetupSubnodeDependencies();
        m_OwningManager->EnqueueTaskNodes(m_RootTasks);
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

    void ThreadManager_Impl1::EnqueueSetupTask_NoLock()
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
        EnqueueTaskNodes_NoLock(m_InitializeTasks);
        m_InitializeTasks.clear();
        EnqueueTaskNode_NoLock(dynamic_cast<TaskNode*>(setupTaskGraph));
        if (!notEnd)
        {
            Stop();
        }
    }

    void ThreadManager_Impl1::InitializeThreadCount(uint32_t threadNum)
    {
        m_WorkerThreads.reserve(threadNum);
        for (uint32_t i = 0; i < threadNum; ++i)
        {
            m_WorkerThreads.emplace_back(&ThreadManager_Impl1::ProcessingWorks, this, i);
        }
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
        {
            std::lock_guard<std::mutex> guard(m_Mutex);
            EnqueueSetupTask_NoLock();
        }
        uint32_t mainThreadID = m_WorkerThreads.size();
        ProcessingWorksMainThread(mainThreadID);
        for (std::thread& itrThread : m_WorkerThreads)
        {
            itrThread.join();
        }
    }

    void ThreadManager_Impl1::Stop()
    {
        {
            m_Stopped = true;
            m_ConditinalVariable.notify_all();
            m_MainthreadCV.notify_one();
        }

    }

    void ThreadManager_Impl1::EnqueueTaskNode(TaskNode* enqueueNode)
    {
        std::lock_guard<std::mutex> guard(m_Mutex);
        EnqueueTaskNode_NoLock(enqueueNode);
    }
    void ThreadManager_Impl1::EnqueueTaskNode_NoLock(TaskNode* enqueueNode)
    {
        if (!enqueueNode->m_Running)
        {
            if (!TryWaitOnEvent(enqueueNode))
            {
                enqueueNode->m_Running.store(true, std::memory_order_relaxed);
                if (enqueueNode->RunOnMainThread())
                {
                    m_MainThreadQueue.push_back(enqueueNode);
                    m_MainthreadCV.notify_one();
                }
                else
                {
                    m_TaskQueue.push_back(enqueueNode);
                    m_ConditinalVariable.notify_one();
                }
            }
        }
    }
    void ThreadManager_Impl1::EnqueueTaskNodes_NoLock(castl::vector<TaskNode*> const& nodeDeque)
    {
        uint32_t anythreadEnqueuedCounter = 0;
        bool mainthreadEnqueued = false;
        for (TaskNode* itrNode : nodeDeque)
        {
            if (!itrNode->m_Running)
            {
                if (!TryWaitOnEvent(itrNode))
                {
                    itrNode->m_Running.store(true, std::memory_order_relaxed);
                    if (itrNode->RunOnMainThread())
                    {
                        mainthreadEnqueued = true;
                        m_MainThreadQueue.push_back(itrNode);
                    }
                    else
                    {
                        m_TaskQueue.push_back(itrNode);
                        ++anythreadEnqueuedCounter;
                    }
                }
            }
        };
        std::atomic_thread_fence(std::memory_order_release);
        if (mainthreadEnqueued)
        {
            m_MainthreadCV.notify_one();
        }
        if (anythreadEnqueuedCounter == 0)
            return;
        if (anythreadEnqueuedCounter > 1)
        {
            m_ConditinalVariable.notify_all();
            m_MainthreadCV.notify_one();
        }
        else
        {
            m_ConditinalVariable.notify_one();
        }
    }

    void ThreadManager_Impl1::EnqueueTaskNodes(eastl::vector<TaskNode*> const& nodeDeque)
    {
        std::lock_guard<std::mutex> guard(m_Mutex);
        EnqueueTaskNodes_NoLock(nodeDeque);
    }
    void ThreadManager_Impl1::SignalEvent(castl::string const& eventName, uint64_t signalFrame)
    {
        std::lock_guard<std::mutex> guard(m_Mutex);
        //CA_LOG_ERR(eventName);
        auto found = m_EventMap.find(eventName);
        if(found == m_EventMap.end())
		{
            found = m_EventMap.insert(castl::make_pair(eventName, m_EventWaitLists.size())).first;
            m_EventWaitLists.emplace_back();
		}
        auto& waitList = m_EventWaitLists[found->second];
        waitList.Signal(signalFrame);
        uint32_t anythreadEnqueuedCounter = 0;
        bool mainthreadEnqueued = false;
        if (eventName == m_SetupEventName)
        {
            EnqueueSetupTask_NoLock();
            ++anythreadEnqueuedCounter;
        }
        if (!waitList.m_WaitingFrames.empty())
        {
            while (!waitList.m_WaitingFrames.empty() && waitList.m_WaitingFrames.front().first <= waitList.m_SignaledFrame)
            {
                for (uint32_t i = 0; i < waitList.m_WaitingFrames.front().second; ++i)
                {
                    TaskNode* waitNode = waitList.m_WaitingTasks.front();
                    waitList.m_WaitingTasks.pop_front();
                    CA_ASSERT(!waitNode->m_Running, "Task is already running");
                    waitNode->m_Running.store(true, std::memory_order_relaxed);
                    if (waitNode->RunOnMainThread())
                    {
                        m_MainThreadQueue.push_back(waitNode);
                        mainthreadEnqueued = true;
                    }
                    else
                    {
                        m_TaskQueue.push_back(waitNode);
                        ++anythreadEnqueuedCounter;
                    }
                }
                waitList.m_WaitingFrames.pop_front();
            }
        }
        if (mainthreadEnqueued)
        {
            m_MainthreadCV.notify_one();
        }
        if (anythreadEnqueuedCounter > 0)
        {
            if (anythreadEnqueuedCounter > 1)
            {
                m_ConditinalVariable.notify_all();
                m_MainthreadCV.notify_one();
            }
            else if (anythreadEnqueuedCounter == 1)
            {
                m_ConditinalVariable.notify_one();
            }
        }
    }
    bool ThreadManager_Impl1::TryWaitOnEvent(TaskNode* node)
    {
        if (!node->m_EventName.empty())
        {
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
                    return true;
                }
			}
        }
        return false;
    }
    void ThreadManager_Impl1::NotifyChildNodeFinish(TaskNode* childNode)
    {
    }
    void ThreadManager_Impl1::ProcessingWorks(uint32_t threadId)
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
                //CA_LOG_ERR("Stopped " + threadId);
                lock.unlock();
                return;
            }
            auto task = m_TaskQueue.front();
            m_TaskQueue.pop_front();
            lock.unlock();
            //std::cout << task->m_Name << std::endl;
            task->Execute_Internal();
        }
    }

    void ThreadManager_Impl1::ProcessingWorksMainThread(uint32_t threadId)
    {
        while (!m_Stopped)
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_MainthreadCV.wait(lock, [this]()
                {
                    //TaskQueue不是空的，或者线程管理器已经停止，不再等待
                    return m_Stopped || !(m_TaskQueue.empty() && m_MainThreadQueue.empty());
                });

            //if (m_TaskQueue.empty() && m_MainThreadQueue.empty())
            //{
            //    lock.unlock();
            //    continue;
            //}
            if (m_Stopped)
            {
                //CA_LOG_ERR("Stopped " + threadId);
                lock.unlock();
                return;
            }
            if (!m_MainThreadQueue.empty())
            {
                auto task = m_MainThreadQueue.front();
                m_MainThreadQueue.pop_front();
                lock.unlock();
                task->Execute_Internal();
            }
            else if (!m_TaskQueue.empty())
            {
                auto task = m_TaskQueue.front();
                m_TaskQueue.pop_front();
                lock.unlock();
                task->Execute_Internal();
            }
        }
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
        //CA_LOG_ERR("Finalize " + m_Name);
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
        //CA_LOG_ERR("Execute " + m_Name);
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
        m_OwningManager->EnqueueTaskNodes(m_TaskList);
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
    void ThreadManager_Impl1::TaskWaitList::Signal(uint64_t signalFrame)
    {
        if (signalFrame > m_SignaledFrame)
        {
            m_SignaledFrame = signalFrame;
        }
    }
}


