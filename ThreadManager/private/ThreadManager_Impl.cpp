#include "pch.h"
#include "ThreadManager_Impl.h"
#include <DebugUtils.h>
#include <CASTL/CAChrono.h>

namespace thread_management
{
    CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(CThreadManager, ThreadManager_Impl)

    thread_local static ThreadLocalData g_ThreadLocalData;
    constexpr uint32_t MAIN_WORKER_ID = 0;

    CTaskGraph* TaskGraph_Impl1::Name(cacore::NameHash name)
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

    CTaskGraph* TaskGraph_Impl1::WaitOnEvent(cacore::NameHash const& name)
    {
        WaitEvent_Internal(name);
        return this;
    }

    CTaskGraph* TaskGraph_Impl1::SignalEvent(cacore::NameHash const& name)
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
        m_ThreadKey = CThreadManager::MainThreadName();
        //m_RunOnMainThread = true;
        return this;
    }

    CTaskGraph* TaskGraph_Impl1::Thread(cacore::NameHash const& threadKey)
    {
        SetThreadKey_Internal(threadKey);
        return this;
    }

    TaskGraph_Impl1::TaskGraph_Impl1(ThreadManager_Impl* owningManager, TaskNodeAllocator* allocator) :
        TaskNode(owningManager, allocator)
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
            CPUTIMER_SCOPE(m_Name.Get().data());
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
        m_ThreadKey = CThreadManager::MainThreadName();
        //m_RunOnMainThread = true;
        return this;
    }
    CTask* CTask_Impl1::Thread(cacore::NameHash const& threadKey)
    {
        SetThreadKey_Internal(threadKey);
        return this;
    }
    CTask* CTask_Impl1::Name(cacore::NameHash name)
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
    CTask* CTask_Impl1::WaitOnEvent(cacore::NameHash const& name)
    {
        WaitEvent_Internal(name);
        return this;
    }
    CTask* CTask_Impl1::SignalEvent(cacore::NameHash const& name)
    {
        SignalEvent_Internal(name);
        return this;
    }

    CTask* CTask_Impl1::Functor(castl::function<void()>&& functor)
    {
        m_Functor = functor;
        return this;
    }
    CTask_Impl1::CTask_Impl1(ThreadManager_Impl* owningManager, TaskNodeAllocator* allocator) :
        TaskNode(owningManager, allocator)
    {
    }

    void CTask_Impl1::Release()
    {
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

    ThreadManager_Impl::ThreadManager_Impl() : 
        m_TaskNodeAllocator(this)
    {
    }

    ThreadManager_Impl::~ThreadManager_Impl()
    {
        Stop();
    }

    void ThreadManager_Impl::EnqueueSetupTask()
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
    }

    void ThreadManager_Impl::InitializeThreadCount(catimer::TimerSystem* timer, uint32_t threadNum)
    {
        catimer::SetGlobalTimerSystem(timer);

        m_TaskWorkers.reserve(threadNum);
        m_WorkerThreads.reserve(threadNum);

        for (uint32_t i = 0; i < threadNum; ++i)
        {
            m_TaskWorkers.emplace_back(this);
        }
        for (uint32_t threadIndex = 1; threadIndex < threadNum; ++threadIndex)
        {
            ThreadLocalData threadLocalData;
            threadLocalData.threadName = L"General Thread " + castl::to_wstring(threadIndex);
            threadLocalData.threadIndex = threadIndex;
            m_WorkerThreads.emplace_back(&GeneralTaskWorker::WorkLoopWithThreadLocalData, &m_TaskWorkers[threadIndex], threadLocalData);
        }
        {
            ThreadLocalData threadLocalData;
            threadLocalData.threadName = L"Main Thread";
            threadLocalData.threadIndex = 0;
            m_TaskWorkers[0].SetThreadLocalData(threadLocalData);
        }

		castl::vector <uint32_t> generalThreadIDs;
        generalThreadIDs.resize(threadNum);
		for (uint32_t i = 0; i < threadNum; ++i)
		{
			generalThreadIDs[i] = i;
		}
        AddTaskQueue(CThreadManager::CommonTaskName(), generalThreadIDs);
        AddTaskQueue(CThreadManager::MainThreadName(), { MAIN_WORKER_ID });
    }
    void ThreadManager_Impl::AddTaskQueue(cacore::NameHash const& name, castl::array_ref<uint32_t> threadIDs)
    {
        size_t queueID;
        {
            castl::unique_lock guard(m_Mutex);
            queueID = m_SharedTaskQueues.size();
            m_DedicateThreadMap.SetThreadIndex(name, queueID);
            m_SharedTaskQueues.emplace_back(this, threadIDs);
        }

		for (uint32_t threadID : threadIDs)
		{
			m_TaskWorkers[threadID].AddQueue(queueID);
		}
    }
    CTask_Impl1* ThreadManager_Impl::NewTask()
    {
        ++m_PendingTaskCount;
        return m_TaskNodeAllocator.NewTask(this);
    }
    TaskParallelFor_Impl* ThreadManager_Impl::NewTaskParallelFor()
    {
        ++m_PendingTaskCount;
        return m_TaskNodeAllocator.NewTaskParallelFor(this);
    }
    TaskGraph_Impl1* ThreadManager_Impl::NewTaskGraph()
    {
        ++m_PendingTaskCount;
        return m_TaskNodeAllocator.NewTaskGraph(this);
    }

    void ThreadManager_Impl::LogStatus() const
    {
        m_TaskNodeAllocator.LogStatus();
    }

    void ThreadManager_Impl::LoopFunction(castl::function<void(TaskScheduler*)> functor, cacore::NameHash const& waitingEvent)
    {
        m_PrepareFunctor = functor;
        m_SetupEventName = waitingEvent;
    }

    castl::shared_ptr<TaskScheduler> ThreadManager_Impl::NewScheduler()
    {
        TaskScheduler_Impl* newScheduler = new TaskScheduler_Impl(this, this, &m_TaskNodeAllocator);
        return castl::shared_ptr<TaskScheduler>(newScheduler, [](TaskScheduler* pScheduler)
            {
                pScheduler->WaitAll();
            });
    }

    void ThreadManager_Impl::Run()
    {
        m_WaitingIdle = false;
        ResetMainThread();
        EnqueueSetupTask();
        ProcessingWorksMainThread();
    }

    void ThreadManager_Impl::Stop()
    {
        {
			castl::unique_lock guard(m_Mutex);
			m_Running = false;
        }
        for (auto& worker : m_TaskWorkers)
        {
            worker.Stop();
        }
        for (std::thread& itrThread : m_WorkerThreads)
        {
            itrThread.join();
        }
    }

    void ThreadManager_Impl::WakeAll()
    {
        for (auto& workers : m_TaskWorkers)
        {
            workers.Notify();
        }
    }

    bool ThreadManager_Impl::IsRunning() noexcept
    {
        //castl::shared_lock lock_guard(m_Mutex);
        return m_Running;
    }

    void ThreadManager_Impl::EnqueueTaskNode(TaskNode* enqueueNode)
    {
        CA_ASSERT(enqueueNode->m_Running.load() == TaskNodeState::ePrepare, "Task Node Not Prepared");
        CA_ASSERT_BREAK(enqueueNode->Valid(), "Invalid Task Node");
		uint32_t queueID = m_DedicateThreadMap.GetThreadIndex(enqueueNode->m_ThreadKey);
		m_SharedTaskQueues[queueID].EnqueueTaskNodes({ enqueueNode });
    }
    
    void ThreadManager_Impl::EnqueueTaskNodes_Loop(castl::array_ref<TaskNode*> nodes)
    {
        for (TaskNode* node : nodes)
        {
            EnqueueTaskNode(node);
        }
    }

    void ThreadManager_Impl::SignalEvent(cacore::NameHash const& eventName, uint64_t signalFrame)
    {
        if (eventName == m_SetupEventName)
        {
            EnqueueSetupTask();
        }
        m_EventManager.SignalEvent(*this, eventName, signalFrame);
    }
    
    void ThreadManager_Impl::NotifyChildNodeFinish(TaskNode* childNode)
    {
        uint32_t resultCount =  m_PendingTaskCount.fetch_sub(1, castl::memory_order_seq_cst);
        if (resultCount == 1)
        {
            StopMainThread();
        }
    }
    
    void ThreadManager_Impl::ResetMainThread()
    {
        m_TaskWorkers[MAIN_WORKER_ID].Reset();
    }

    void ThreadManager_Impl::StopMainThread()
    {
		m_TaskWorkers[MAIN_WORKER_ID].Stop();
    }

    void ThreadManager_Impl::ProcessingWorksMainThread()
    {
        m_TaskWorkers[0].WorkLoop();
    }

    TaskParallelFor* TaskParallelFor_Impl::Name(cacore::NameHash name)
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

    TaskParallelFor* TaskParallelFor_Impl::WaitOnEvent(cacore::NameHash const& name)
    {
        WaitEvent_Internal(name);
        return this;
    }

    TaskParallelFor* TaskParallelFor_Impl::SignalEvent(cacore::NameHash const& name)
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

    TaskParallelFor_Impl::TaskParallelFor_Impl(ThreadManager_Impl* owningManager, TaskNodeAllocator* allocator) :
        TaskNode(owningManager, allocator)
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
    TaskNodeAllocator::TaskNodeAllocator(ThreadManager_Impl* owningManager) :  
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
   
    void TaskNodeAllocator::Release(TaskBase* childNode)
    {
        castl::atomic_thread_fence(castl::memory_order_acq_rel);
        CA_ASSERT(dynamic_cast<TaskNode*>(childNode)->GetState() != TaskNodeState::eInvalid, "Release Invalid Node");
        switch (childNode->GetType())
        {
        case TaskNodeType::eGraph:
        {
            m_TaskGraphPool.Release(static_cast<TaskGraph_Impl1*>(childNode));
            --m_Counter;
            break;
        }
        case TaskNodeType::eNode:
        {
            m_TaskPool.Release(static_cast<CTask_Impl1*>(childNode));
            --m_Counter;
            break;
        }
        case TaskNodeType::eNodeParallel:
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

    /*void DedicateTaskQueue::Stop()
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
    }*/
    //void DedicateTaskQueue::EnqueueTaskNodes(castl::array_ref<TaskNode*> const& nodeDeque)
    //{
    //    {
    //        castl::lock_guard<castl::mutex> guard(m_Mutex);
    //        EnqueueTaskNodes_NoLock(nodeDeque);
    //        m_ConditionalVariable.notify_all();
    //    }
    //}
    //void DedicateTaskQueue::EnqueueTaskNodes_NoLock(castl::array_ref<TaskNode*> const& nodeDeque)
    //{
    //    for (TaskNode* itrNode : nodeDeque)
    //    {
    //        m_Queue.push_back(itrNode);
    //    }
    //}
    void TaskNodeEventManager::SignalEvent(ThreadManager_Impl& threadManager, cacore::NameHash const& eventKey, uint64_t signalFrame)
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
        if (!node->m_EventName.Valid())
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

    TaskScheduler_Impl::TaskScheduler_Impl(TaskBaseObject* owner, ThreadManager_Impl* owningManager, TaskNodeAllocator* allocator)
        : m_Owner(owner), m_OwningManager(owningManager), m_Allocator(allocator)
    {
        m_EntryWorkerID = g_ThreadLocalData.threadIndex;
    }

    void TaskScheduler_Impl::Execute(castl::array_ref<TaskBase*> nodes, bool wait)
    {
        {
            castl::unique_lock guard(m_Mutex);
            int32_t taskCount = 0;
            for (TaskBase* node : nodes)
            {
                TaskNode* taskNode = dynamic_cast<TaskNode*>(node);
                if (taskNode->WaitingToRun(this))
                {
                    taskNode->SetupThisNodeDependencies_Internal();
                    ++taskCount;
                }
            }
            if (taskCount == 0)
                return;
            m_PendingTaskCount = taskCount;
        }
        for (TaskBase* node : nodes)
        {
            TaskNode* taskNode = dynamic_cast<TaskNode*>(node);
            if (taskNode->WaitingToRun(this) && taskNode->GetDepenedentCount() == 0)
            {
                m_OwningManager->EnqueueTaskNode(taskNode);
            }
        }

        if (wait)
        {
            auto& threadLocalQueue = m_OwningManager->GetTaskWorker(m_EntryWorkerID);
            threadLocalQueue.InlineWorkLoop(this);
        }
    }

    void TaskScheduler_Impl::Finalize()
    {
        Execute(m_SubTasks, true);
        {
            castl::unique_lock guard(m_Mutex);
            m_SubTasks.clear();
        }
    }

    bool TaskScheduler_Impl::IsFinished()
    {
		//castl::shared_lock guard(m_Mutex);
        return m_PendingTaskCount == 0;
    }

    void TaskScheduler_Impl::NotifyChildNodeFinish(TaskNode* childNode)
    {
        //int32_t pendingCount;
        //{
        //    castl::unique_lock guard(m_Mutex);
        //    --m_PendingTaskCount;
        //    pendingCount = m_PendingTaskCount;
        //}
        int32_t pendingCount = m_PendingTaskCount.fetch_sub(1, castl::memory_order_seq_cst);
        if (pendingCount <= 1)
        {
            m_OwningManager->GetTaskWorker(m_EntryWorkerID).Notify();
        }
    }
    CTask* TaskScheduler_Impl::NewTask()
    {
        auto result = m_Allocator->NewTask(this);
        {
            castl::unique_lock guard(m_Mutex);
            m_SubTasks.push_back(result);
        }
        return result;
    }
    TaskParallelFor* TaskScheduler_Impl::NewTaskParallelFor()
    {
        auto result = m_Allocator->NewTaskParallelFor(this);
        {
            castl::unique_lock guard(m_Mutex);
            m_SubTasks.push_back(result);
        }
        return result;
    }
    CTaskGraph* TaskScheduler_Impl::NewTaskGraph()
    {
        auto result = m_Allocator->NewTaskGraph(this);
        {
            castl::unique_lock guard(m_Mutex);
            m_SubTasks.push_back(result);
        }
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
    GeneralTaskWorker::GeneralTaskWorker()
    {
    }
    GeneralTaskWorker::GeneralTaskWorker(ThreadManager_Impl* owningManager) :
        m_OwningManager(owningManager)
    {
    }
    GeneralTaskWorker::GeneralTaskWorker(GeneralTaskWorker&& other) noexcept :
        m_OwningManager(other.m_OwningManager)
		, m_WorkerConditionalVariable()
        , m_Queues(std::move(other.m_Queues))
    {
    }
    void GeneralTaskWorker::Notify()
    {
        castl::unique_lock<castl::mutex> lock(m_WorkerMutex);
        m_WorkerConditionalVariable.notify_one();
    }
    void GeneralTaskWorker::Stop()
    {
        {
            castl::unique_lock<castl::mutex> lock(m_WorkerMutex);
            m_Stop = true;
        }
        Notify();
    }
    void GeneralTaskWorker::Reset()
	{
		castl::unique_lock<castl::mutex> lock(m_WorkerMutex);
		m_Stop = false;
    }
    void GeneralTaskWorker::SetThreadLocalData(ThreadLocalData const& threadLocalData)
    {
        g_ThreadLocalData = threadLocalData;

        HRESULT r;
        r = SetThreadDescription(
            GetCurrentThread(),
            g_ThreadLocalData.threadName.c_str()
        );
    }
    void GeneralTaskWorker::WorkLoopWithThreadLocalData(ThreadLocalData const& threadLocalData)
    {
		SetThreadLocalData(threadLocalData);
		WorkLoop();
    }
    void GeneralTaskWorker::WorkLoop()
    {
        castl::vector<castl::shared_lock<castl::shared_mutex>> queue_locks;
        while (true)
        {
            CPUTIMER_SCOPE("Task WorkLoop");
            {
                {
                    CPUTIMER_SCOPE("Idle");
                    castl::unique_lock<castl::mutex> lock(m_WorkerMutex);
                    queue_locks.clear();
                    queue_locks.reserve(m_Queues.size());
                    m_WorkerConditionalVariable.wait(lock, [this, &queue_locks]()
                    {
                        //È«²¿Ëø×¡ÔÙÅÐ¶Ï
                        castl::shared_lock manager_lock(m_OwningManager->GetMutex());
                        for (auto queueID : m_Queues)
                        {
                            queue_locks.emplace_back(m_OwningManager->GetSharedTaskQueue(queueID).GetMutex());
                        }
                        bool result = m_Stop || (!m_OwningManager->IsRunning());
                        for (auto queueID : m_Queues)
                        {
                            result = result || !m_OwningManager->GetSharedTaskQueue(queueID).GetQueue().empty();
                        }
                        m_Pause = !result;
                        queue_locks.clear();
                        return result;
                    });
                }

                if (m_Stop || !m_OwningManager->IsRunning())
                    return;

                for (auto queueID : m_Queues)
                {
					auto& queue = m_OwningManager->GetSharedTaskQueue(queueID);
                    TaskNode* pNode = nullptr;
                    if (queue.TryDeque(pNode))
                    {
                        pNode->Execute_Internal();
                        pNode->ReleaseSelf();
                    }
                }
            }
        }
    }
    void GeneralTaskWorker::InlineWorkLoop(TaskScheduler_Impl* taskScheduler)
    {

        castl::vector<castl::shared_lock<castl::shared_mutex>> queue_locks;
        while (true)//(m_OwningManager->IsRunning() && !taskScheduler->IsFinished())
        {
            CPUTIMER_SCOPE("Inline WorkLoop");
            {
                {
                    CPUTIMER_SCOPE("Idle");
                    castl::unique_lock<castl::mutex> lock(m_WorkerMutex);
                    queue_locks.clear();
                    queue_locks.reserve(m_Queues.size());
                    m_WorkerConditionalVariable.wait(lock, [this, taskScheduler, &queue_locks]()
                    {
                        //È«²¿Ëø×¡ÔÙÅÐ¶Ï
                        castl::shared_lock scheduler_lock(taskScheduler->GetMutex());
                        castl::shared_lock manager_lock(m_OwningManager->GetMutex());
                        for (auto queueID : m_Queues)
                        {
                            queue_locks.emplace_back(m_OwningManager->GetSharedTaskQueue(queueID).GetMutex());
                        }

                        bool result = m_Stop
                            || taskScheduler->IsFinished()
                            || (!m_OwningManager->IsRunning());
                        for (auto queueID : m_Queues)
                        {
                            result = result || !m_OwningManager->GetSharedTaskQueue(queueID).GetQueue().empty();
                        }
                        m_Pause = !result;
                        queue_locks.clear();
                        return result;
                    });
                }

                if (m_Stop || taskScheduler->IsFinished() || !m_OwningManager->IsRunning())
                    return;

                for (auto queueID : m_Queues)
                {
                    auto& queue = m_OwningManager->GetSharedTaskQueue(queueID);
                    TaskNode* pNode = nullptr;
                    if (queue.TryDeque(pNode))
                    {
                        pNode->Execute_Internal();
                        pNode->ReleaseSelf();
                    }
                }
            }
        }
    }

    void GeneralTaskWorker::AddQueue(size_t queueID)
    {
		castl::unique_lock lock(m_WorkerMutex);
		m_Queues.push_back(queueID);
    }

	SharedTaskQueue::SharedTaskQueue(ThreadManager_Impl* owningManager, castl::array_ref<uint32_t> worker) :
        m_OwningManager(owningManager)
    {
		m_WorkerIDs.resize(worker.size());
		for (size_t i = 0; i < worker.size(); ++i)
		{
			m_WorkerIDs[i] = worker.data()[i];
		}
    }

    SharedTaskQueue::SharedTaskQueue(SharedTaskQueue&& other) noexcept :
        m_Queue(std::move(other.m_Queue))
        , m_WorkerIDs(std::move(other.m_WorkerIDs))
		, m_LastWorkerID(other.m_LastWorkerID)
		, m_OwningManager(other.m_OwningManager)
    {
    }

    bool SharedTaskQueue::TryDeque(TaskNode*& outNode)
    {
        {
            castl::shared_lock lock(m_QueueMutex);
			if (m_Queue.empty())
				return false;
        }
        {
			castl::unique_lock lock(m_QueueMutex);
			if (m_Queue.empty())
				return false;
			outNode = m_Queue.front();
			m_Queue.pop_front();
            return true;
        }
        return false;
    }
    void SharedTaskQueue::EnqueueTaskNodes(castl::array_ref<TaskNode*> const& nodeDeque)
    {
        if (nodeDeque.empty())
            return;
        {
            size_t notifyCount;
            size_t lastWorkID;
            {
                castl::unique_lock lock(m_QueueMutex);
                for (TaskNode* itrNode : nodeDeque)
                {
                    m_Queue.push_back(itrNode);
                }
                notifyCount = (castl::min)(nodeDeque.size(), m_WorkerIDs.size());
				lastWorkID = m_LastWorkerID;
				m_LastWorkerID = (m_LastWorkerID + notifyCount) % m_WorkerIDs.size();
            }
            {
                for (size_t i = 0; i < notifyCount; ++i)
                {
                    lastWorkID = (lastWorkID + 1) % m_WorkerIDs.size();
                    m_OwningManager->GetTaskWorker(lastWorkID).Notify();
                }
            }

        }
    }
}


