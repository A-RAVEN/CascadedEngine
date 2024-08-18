#include "pch.h"
#include "ThreadManager_Impl.h"
#include <DebugUtils.h>
#include <CASTL/CAChrono.h>

namespace thread_management
{
    CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(CThreadManager, ThreadManager_Impl1)

    thread_local static ThreadLocalData g_ThreadLocalData;
    constexpr uint32_t MAIN_QUEUE_ID = 0;
    constexpr uint32_t GENERAL_QUEUE_ID = 1;

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

    //CTaskGraph* TaskGraph_Impl1::SetupFunctor(castl::function<void(CTaskGraph* thisGraph)> functor)
    //{
    //    m_Functor = functor;
    //    return this;
    //}

    CTaskGraph* TaskGraph_Impl1::Func(castl::function<void(TaskScheduler*)> functor)
    {
        m_ScheduleFunctor = functor;
        return this;
    }

    CTaskGraph* TaskGraph_Impl1::MainThread()
    {
        m_RunOnMainThread = true;
        return this;
    }

    CTaskGraph* TaskGraph_Impl1::Thread(cacore::HashObj<castl::string> const& threadKey)
    {
        SetThreadKey_Internal(threadKey);
        return this;
    }

    /*CTask* TaskGraph_Impl1::NewTask()
    {
        auto result = m_Allocator->NewTask(this);
        result->Thread(m_ThreadKey);
        result->SetRunOnMainThread(m_RunOnMainThread);
        m_SubTasks.push_back(result);
        return result;
    }

    TaskParallelFor* TaskGraph_Impl1::NewTaskParallelFor()
    {
        auto result = m_Allocator->NewTaskParallelFor(this);
        result->SetRunOnMainThread(m_RunOnMainThread);
        m_SubTasks.push_back(result);
        return result;
    }

    CTaskGraph* TaskGraph_Impl1::NewTaskGraph()
    {
        auto result = m_Allocator->NewTaskGraph(this);
        result->Thread(m_ThreadKey);
        result->SetRunOnMainThread(m_RunOnMainThread);
        m_SubTasks.push_back(result);
        return result;
    }*/

    TaskGraph_Impl1::TaskGraph_Impl1(ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator) :
        TaskNode(TaskObjectType::eGraph, owningManager, allocator)
    {
    }

    void TaskGraph_Impl1::Release()
    {
        m_ScheduleFunctor = nullptr;
        Release_Internal();
    }

    void TaskGraph_Impl1::NotifyChildNodeFinish(TaskNode* childNode)
    {
    }
    void TaskGraph_Impl1::Execute_Internal()
    {
        {
            CPUTIMER_SCOPE(m_Name.c_str());
            if (m_ScheduleFunctor != nullptr)
            {
                TaskScheduler_Impl taskScheduler(this, m_OwningManager, m_Allocator);
                m_ScheduleFunctor(&taskScheduler);
                taskScheduler.Finalize();
            }
        }
        FinalizeExecution_Internal();
    }

    
    CTask* CTask_Impl1::MainThread()
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

    CTask* CTask_Impl1::Functor(castl::function<void()>&& functor)
    {
        m_Functor = functor;
        return this;
    }
    CTask_Impl1::CTask_Impl1(ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator) :
        TaskNode(TaskObjectType::eNode, owningManager, allocator)
    {
    }

    void CTask_Impl1::Release()
    {
        //castl::lock_guard<castl::mutex> guard(m_Mutex);
        m_RunOnMainThread = false;
        m_Functor = nullptr;
        Release_Internal();
    }

    void CTask_Impl1::Execute_Internal()
    {
        //castl::lock_guard<castl::mutex> guard(m_Mutex);
        if (m_Functor != nullptr)
        {
            CPUTIMER_SCOPE(m_Name.c_str());
            m_Functor();
        }
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
    }

    void ThreadManager_Impl1::EnqueueOneTimeTasks()
    {
        EnqueueTaskNodes_Loop(m_InitializeTasks);
        m_InitializeTasks.clear();
    }

    void ThreadManager_Impl1::EnqueueSetupTask()
    {
        if (m_WaitingIdle)
            return;
        if (m_PrepareFunctor == nullptr)
        {
            m_WaitingIdle = true;
            return;
        }
        TaskGraph_Impl1* setupTaskGraph = NewTaskGraph();
        setupTaskGraph->Name("Setup");
        setupTaskGraph->SignalEvent(m_SetupEventName);
        setupTaskGraph->Func(m_PrepareFunctor);
        ++m_Frames;
        EnqueueTaskNode(setupTaskGraph);
        //if (!notEnd)
        //{
        //    m_WaitingIdle = true;
        //}
    }

    void ThreadManager_Impl1::InitializeThreadCount(catimer::TimerSystem* timer, uint32_t threadNum, uint32_t dedicateThreadNum)
    {
        catimer::SetGlobalTimerSystem(timer);
        m_WorkerThreads.reserve(threadNum + dedicateThreadNum);
        uint32_t taskQueueNum = threadNum + dedicateThreadNum + 1;
        m_DedicateTaskQueues.resize(taskQueueNum);
        m_DedicateThreadMap.SetThreadIndex(castl::string{ "MainThread" }, 0);
        m_DedicateThreadMap.SetThreadIndex(castl::string{ "GeneralThread" }, 1);

        uint32_t threadIndex = 1;
        for (uint32_t i = 0; i < threadNum; ++i)
        {
            ThreadLocalData threadLocalData;
            threadLocalData.threadName = L"General Thread " + castl::to_wstring(i);
            threadLocalData.queueIndex = GENERAL_QUEUE_ID;
            threadLocalData.threadIndex = threadIndex;
            m_WorkerThreads.emplace_back(&DedicateTaskQueue::WorkLoop, &m_DedicateTaskQueues[GENERAL_QUEUE_ID], threadLocalData);
            ++threadIndex;
        }

        uint32_t queueID = 2;
        for (uint32_t i = 0; i < dedicateThreadNum; ++i)
        {
            ThreadLocalData threadLocalData;
            threadLocalData.threadName = L"Dedicate Thread " + castl::to_wstring(i);
            threadLocalData.queueIndex = queueID;
            threadLocalData.threadIndex = threadIndex;

			m_WorkerThreads.emplace_back(&DedicateTaskQueue::WorkLoop, &m_DedicateTaskQueues[queueID], threadLocalData);
            ++queueID;
            ++threadIndex;
		}
    }
    void ThreadManager_Impl1::SetDedicateThreadMapping(uint32_t dedicateThreadIndex, cacore::HashObj<castl::string> const& name)
    {
        m_DedicateThreadMap.SetThreadIndex(name, dedicateThreadIndex + 1);
    }
    CTask_Impl1* ThreadManager_Impl1::NewTask()
    {
        ++m_PendingTaskCount;
        return m_TaskNodeAllocator.NewTask(this);
    }
    TaskParallelFor_Impl* ThreadManager_Impl1::NewTaskParallelFor()
    {
        ++m_PendingTaskCount;
        return m_TaskNodeAllocator.NewTaskParallelFor(this);
    }
    TaskGraph_Impl1* ThreadManager_Impl1::NewTaskGraph()
    {
        ++m_PendingTaskCount;
        return m_TaskNodeAllocator.NewTaskGraph(this);
    }

    void ThreadManager_Impl1::LogStatus() const
    {
        m_TaskNodeAllocator.LogStatus();
    }

    void ThreadManager_Impl1::OneTime(castl::function<void(TaskScheduler*)> functor, castl::string const& waitingEvent)
    {
        if (functor == nullptr)
            return;
        auto* newTaskGraph = NewTaskGraph();
        newTaskGraph->Func(functor);
        castl::lock_guard<castl::mutex> guard(m_Mutex);
        m_InitializeTasks.push_back(newTaskGraph);
    }

    void ThreadManager_Impl1::LoopFunction(castl::function<void(TaskScheduler*)> functor, castl::string const& waitingEvent)
    {
        m_PrepareFunctor = functor;
        m_SetupEventName = waitingEvent;
    }

    void ThreadManager_Impl1::Run()
    {
        m_WaitingIdle = false;
        ResetMainThread();
        EnqueueOneTimeTasks();
        EnqueueSetupTask();
        ProcessingWorksMainThread();
    }

    void ThreadManager_Impl1::Stop()
    {
        for (auto& dedicateThread : m_DedicateTaskQueues)
        {
            dedicateThread.Stop();
        }
        //m_Stopped = true;
        //m_ConditinalVariable.notify_all();
        for (std::thread& itrThread : m_WorkerThreads)
        {
            itrThread.join();
        }
    }

    void ThreadManager_Impl1::WakeAll()
    {
        for (auto& dedicateThread : m_DedicateTaskQueues)
        {
            dedicateThread.NotifyAll();
        }
    }

    void ThreadManager_Impl1::EnqueueTaskNode(TaskNode* enqueueNode)
    {
        //std::cout << "Enqueue" << std::endl;
        CA_ASSERT(enqueueNode->m_Running.load() == TaskNodeState::ePrepare, "Invalid Task Node");
        CA_ASSERT_BREAK(enqueueNode->Valid(), "Invalid Task Node");
        if(enqueueNode->m_ThreadKey.Valid() || enqueueNode->m_RunOnMainThread)
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

    //void ThreadManager_Impl1::EnqueueTaskNodes_GeneralThread(castl::array_ref<TaskNode*> nodes)
    //{
    //    for (TaskNode* itrNode : nodes)
    //    {
    //        itrNode->m_Running.store(true, castl::memory_order_relaxed);
    //    }
    //    m_DedicateTaskQueues[GENERAL_QUEUE_ID].EnqueueTaskNodes(nodes);
    //}
    void ThreadManager_Impl1::EnqueueTaskNode_GeneralThread(TaskNode* node)
    {
        node->m_Running.store(TaskNodeState::ePending, castl::memory_order_seq_cst);
        if (m_EventManager.WaitEventDone(node))
        {
            m_DedicateTaskQueues[GENERAL_QUEUE_ID].EnqueueTaskNodes(node);
        }
    }
    void ThreadManager_Impl1::EnqueueTaskNode_DedicateThread(TaskNode* node)
    {
        node->m_Running.store(TaskNodeState::ePending, castl::memory_order_seq_cst);
        if (m_EventManager.WaitEventDone(node))
        {
            uint32_t dedicateThreadID = node->m_RunOnMainThread ? 0u : m_DedicateThreadMap.GetThreadIndex(node->m_ThreadKey) % m_DedicateTaskQueues.size();
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
        uint32_t resultCount =  m_PendingTaskCount.sub_fetch(1, castl::memory_order_seq_cst);
        if (resultCount == 0)
        {
            StopMainThread();
        }
    }
    
    void ThreadManager_Impl1::ResetMainThread()
    {
        m_DedicateTaskQueues[MAIN_QUEUE_ID].Reset();
    }

    void ThreadManager_Impl1::StopMainThread()
    {
		m_DedicateTaskQueues[MAIN_QUEUE_ID].Stop();
    }

    void ThreadManager_Impl1::ProcessingWorksMainThread()
    {
        ThreadLocalData threadLocalData;
        threadLocalData.threadName = L"Main Thread";
        threadLocalData.queueIndex = 0;
        threadLocalData.threadIndex = 0;
        m_DedicateTaskQueues[0].WorkLoop(threadLocalData);
    }



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

    TaskParallelFor* TaskParallelFor_Impl::Functor(castl::function<void(uint32_t)> functor)
    {
        m_Functor = functor;
        return this;
    }

    TaskParallelFor* TaskParallelFor_Impl::JobCount(uint32_t jobCount)
    {
        m_JobCount.store(jobCount, castl::memory_order_release);
        return this;
    }

    TaskParallelFor_Impl::TaskParallelFor_Impl(ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator) :
        TaskNode(TaskObjectType::eNodeParallel, owningManager, allocator)
    {
    }

    void TaskParallelFor_Impl::Release()
    {
        m_JobCount.store(0, castl::memory_order_seq_cst);
        //m_TaskList.clear();
        Release_Internal();
    }

    void TaskParallelFor_Impl::NotifyChildNodeFinish(TaskNode* childNode)
    {

    }

    void TaskParallelFor_Impl::Execute_Internal()
    {
        CPUTIMER_SCOPE(m_Name.c_str());
        if (m_JobCount > 0 && m_Functor != nullptr)
        {
            TaskScheduler_Impl taskScheduler(this, m_OwningManager, m_Allocator);
            for (uint32_t taskId = 0; taskId < m_JobCount; ++taskId)
            {
                taskScheduler.NewTask()
                    ->Name("Parallal For Task")
                    ->Functor([functor = m_Functor, taskId]()
                    {
                        functor(taskId);
                    });
            }
            taskScheduler.Finalize();
        }
        FinalizeExecution_Internal();

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
        auto result = m_TaskPool.Alloc(m_OwningManager, this);
        result->SetOwner(owner);
        ++m_Counter;
        castl::atomic_thread_fence(castl::memory_order_release);
        return result;
    }
    TaskParallelFor_Impl* TaskNodeAllocator::NewTaskParallelFor(TaskBaseObject* owner)
    {
        auto result = m_TaskParallelForPool.Alloc(m_OwningManager, this);
        result->SetOwner(owner);
        ++m_Counter;
        //CA_ASSERT(result->m_Owner != nullptr, "NULL Owner");
        return result;
    }
    TaskGraph_Impl1* TaskNodeAllocator::NewTaskGraph(TaskBaseObject* owner)
    {
        auto result = m_TaskGraphPool.Alloc(m_OwningManager, this);
        result->SetOwner(owner);
        ++m_Counter;
        //CA_ASSERT(result->m_Owner != nullptr, "NULL Owner");
        return result;
    }
   
    void TaskNodeAllocator::Release(TaskNode* childNode)
    {
        castl::atomic_thread_fence(castl::memory_order_acq_rel);
        switch (childNode->GetTaskObjectType())
        {
        case TaskObjectType::eGraph:
        {
            m_TaskGraphPool.Release(static_cast<TaskGraph_Impl1*>(childNode));
            --m_Counter;
            break;
        }
        case TaskObjectType::eNode:
        {
            m_TaskPool.Release(static_cast<CTask_Impl1*>(childNode));
            --m_Counter;
            break;
        }
        case TaskObjectType::eNodeParallel:
        {
            m_TaskParallelForPool.Release(static_cast<TaskParallelFor_Impl*>(childNode));
            --m_Counter;
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

    void DedicateTaskQueue::Stop()
    {
        {
            castl::lock_guard<castl::mutex> guard(m_Mutex);
            m_Stop = true;
            m_ConditionalVariable.notify_all();
        }
    }
    void DedicateTaskQueue::NotifyAll()
    {
        castl::lock_guard<castl::mutex> guard(m_Mutex);
        m_ConditionalVariable.notify_all();
    }
    void DedicateTaskQueue::Reset()
    {
        castl::lock_guard<castl::mutex> guard(m_Mutex);
		m_Stop = false;
    }
    void DedicateTaskQueue::InlineWorkLoop(TaskScheduler_Impl* taskScheduler)
    {
        CPUTIMER_SCOPE("Inline WorkLoop");
        castl::atomic_thread_fence(castl::memory_order_acq_rel);
        while (!(m_Stop || taskScheduler->IsFinished()))
        {
            TaskNode* pNode = nullptr;
            {
                castl::unique_lock<castl::mutex> lock(m_Mutex);
                m_ConditionalVariable.wait_for(lock, std::chrono::seconds(3), [this, taskScheduler]()
                    {
                        if (m_Stop)
                            return true;
                        if (!m_Queue.empty())
                            return true;
                        if (taskScheduler->IsFinished())
                            return true;
                        else
                            return false;
                    });
                if (m_Stop || taskScheduler->IsFinished())
                {
                    return;
                }
                if (m_Queue.empty())
                    continue;
                pNode = m_Queue.front();
                m_Queue.pop_front();
            }
            if (pNode)
            {
                pNode->Execute_Internal();
                pNode->ReleaseSelf();
            }
        }
    }
    void DedicateTaskQueue::WorkLoop(ThreadLocalData const& threadLocalData)
    {
        g_ThreadLocalData = threadLocalData;

        HRESULT r;
        r = SetThreadDescription(
            GetCurrentThread(),
            g_ThreadLocalData.threadName.c_str()
        );

        while (!m_Stop)
        {
            TaskNode* pNode = nullptr;
            {
                castl::unique_lock<castl::mutex> lock(m_Mutex);
                m_ConditionalVariable.wait(lock, [this]()
                    {
                        return m_Stop || !m_Queue.empty();
                    });
                if (m_Queue.empty() || m_Stop)
                {
                    continue;
                }
                pNode = m_Queue.front();
                m_Queue.pop_front();
            }
            if (pNode)
            {
                pNode->Execute_Internal();
                pNode->ReleaseSelf();
            }
        }
    }
    void DedicateTaskQueue::EnqueueTaskNodes(castl::array_ref<TaskNode*> const& nodeDeque)
    {
        {
            castl::lock_guard<castl::mutex> guard(m_Mutex);
            EnqueueTaskNodes_NoLock(nodeDeque);
            m_ConditionalVariable.notify_all();
        }
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

    TaskScheduler_Impl::TaskScheduler_Impl(TaskBaseObject* owner, ThreadManager_Impl1* owningManager, TaskNodeAllocator* allocator)
        : TaskBaseObject(TaskObjectType::eTaskScheduler), m_Owner(owner), m_OwningManager(owningManager), m_Allocator(allocator)
    {
        m_HoldingQueueID = g_ThreadLocalData.queueIndex;
    }

    void TaskScheduler_Impl::Execute(castl::array_ref<TaskNode*> nodes)
    {
        int32_t taskCount = 0;
        for(TaskNode* node : nodes)
		{
            if (node->WaitingToRun(this))
            {
                node->SetupThisNodeDependencies_Internal();
                ++taskCount;
            }
		}
        if(taskCount == 0)
			return;
        m_PendingTaskCount.store(taskCount, castl::memory_order_release);
        for (TaskNode* node : nodes)
        {
            if (node->GetDepenedentCount() == 0)
            {
                m_OwningManager->EnqueueTaskNode(node);
            }
        }

        auto& threadLocalQueue = m_OwningManager->GetDedicateTaskQueue(m_HoldingQueueID);
        threadLocalQueue.InlineWorkLoop(this);
    }

    void TaskScheduler_Impl::Finalize()
    {
        Execute(m_SubTasks);
        m_SubTasks.clear();
    }

    void TaskScheduler_Impl::NotifyChildNodeFinish(TaskNode* childNode)
    {
        int queueID = m_HoldingQueueID.load(castl::memory_order_seq_cst);
        auto resultCount = m_PendingTaskCount.sub_fetch(1, castl::memory_order_seq_cst);
        if (IsFinished())
        {
            //m_OwningManager->WakeAll();
            m_OwningManager->GetDedicateTaskQueue(queueID).NotifyAll();
        }
    }
    CTask* TaskScheduler_Impl::NewTask()
    {
        auto result = m_Allocator->NewTask(this);
        m_SubTasks.push_back(result);
        return result;
    }
    TaskParallelFor* TaskScheduler_Impl::NewTaskParallelFor()
    {
        auto result = m_Allocator->NewTaskParallelFor(this);
        m_SubTasks.push_back(result);
        return result;
    }
    CTaskGraph* TaskScheduler_Impl::NewTaskGraph()
    {
        auto result = m_Allocator->NewTaskGraph(this);
        m_SubTasks.push_back(result);
        return result;
    }
    void TaskScheduler_Impl::WaitAll()
    {
        Finalize();
    }
    uint64_t TaskScheduler_Impl::GetCurrentFrame() const
    {
        return m_Owner->GetCurrentFrame();
    }
}


