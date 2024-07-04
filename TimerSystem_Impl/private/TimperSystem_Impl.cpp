
#include <CATimer/Timer.h>
#include <CASTL/CAStack.h>
#include <CASTL/CAAtomic.h>
#include <CASTL/CAMutex.h>
#include <CASTL/CADeque.h>
#include <CASTL/CAString.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAChrono.h>
#include <Hasher.h>
namespace catimer
{
	using TimerType = castl::chrono::high_resolution_clock;
	struct TimerFrameHistories;
	class TimerSystem_Impl;

	struct EventHandle
	{
		uint32_t handleID;
		castl::string_view name;
	};
	class FrameData
	{
	public:
		struct EventData
		{
			EventHandle handle;
			uint32_t stackID;
			TimerType::time_point beginTime;
			TimerType::time_point endTime;
		};

		void AddEvent(EventHandle const& inEvent, uint32_t stackID, TimerType::time_point beginTime, TimerType::time_point endTime)
		{
			m_EventRecords.push_back({ inEvent, stackID, beginTime, endTime });
		}
		void Clear()
		{
			m_EventRecords.clear();
		}
		castl::vector<EventData> m_EventRecords;
	};


	struct TimerFrameHistories
	{
		struct ThreadFrameHistories
		{
			void PushFrameData(uint32_t frameID, uint32_t preserveFrameCount, FrameData const& frameData)
			{
				m_FrameData.push_back(castl::make_pair(frameID, frameData));
				uint32_t firstFrame = frameID - (castl::min)(preserveFrameCount, frameID);
				while ((!m_FrameData.empty()) && m_FrameData.front().first < firstFrame)
				{
					m_FrameData.pop_front();
				}
			}
			castl::deque<castl::pair<uint32_t, FrameData>> m_FrameData;
		};

		void SubmitFrame(uint32_t threadID, uint32_t frameID, FrameData const& frameData)
		{
			castl::shared_lock<castl::shared_mutex> lock(m_Mutex);
			m_ThreadFrameHistories[threadID].PushFrameData(frameID, m_PreserveFrameCount, frameData);
		}

		uint32_t RegiterThread()
		{
			castl::unique_lock<castl::shared_mutex> lock(m_Mutex);
			m_ThreadFrameHistories.emplace_back();
			return m_ThreadFrameHistories.size() - 1;
		}

		uint32_t m_PreserveFrameCount = 10;
		castl::shared_mutex m_Mutex;
		castl::vector<ThreadFrameHistories> m_ThreadFrameHistories;
	};


	class EventStack
	{
	public:
		struct EventData
		{
			EventHandle handle;
			TimerType::time_point beginTime;
		};

		void PushEvent(EventHandle const& inEvent)
		{
			m_EventStack.push(castl::make_pair( inEvent, m_Timer.now()));
		}
		void PopEvent(FrameData& frameData)
		{
			uint32_t stackID = m_EventStack.size() - 1;
			castl::pair<EventHandle, TimerType::time_point> eventRecord = m_EventStack.top();
			m_EventStack.pop();
			frameData.AddEvent(eventRecord.first, stackID, eventRecord.second, m_Timer.now());
		}
		TimerType m_Timer;
		castl::stack<castl::pair<EventHandle, TimerType::time_point>> m_EventStack;
	};

	class ThreadLocalStorage
	{
	public:
		static ThreadLocalStorage& Get()
		{
			static thread_local ThreadLocalStorage tls{};
			return tls;
		}

		void SetName(castl::string_view name)
		{
			m_Name = name;
		}

		void EndEvent(TimerFrameHistories& inHistories, uint32_t frameCount)
		{
			CheckInitialize(inHistories);
			UpdateFrameData(inHistories, frameCount);
			m_EventStack.PopEvent(m_FrameData);
		}

		void BeginEvent(EventHandle const& eventHandle)
		{
			m_EventStack.PushEvent(eventHandle);
		}


	private:
		void CheckInitialize(TimerFrameHistories& inHistories)
		{
			if (m_ThreadID < 0)
			{
				m_ThreadID = inHistories.RegiterThread();
			}
		}
		void UpdateFrameData(TimerFrameHistories& inHistories, uint32_t frameCount)
		{
			if (m_CurrentFrameCount < frameCount)
			{
				m_CurrentFrameCount = frameCount;
				inHistories.SubmitFrame(m_ThreadID, m_CurrentFrameCount, m_FrameData);
				m_FrameData.Clear();
			}
		}

		int32_t m_ThreadID = -1;
		uint32_t m_CurrentFrameCount = 0;
		EventStack m_EventStack{};
		FrameData m_FrameData{};
		castl::string m_Name = "";
		friend class TimerSystem_Impl;
	};

	struct EventHandlePool
	{
		EventHandle const& GetOrCreateEventHandle(cacore::HashObj<castl::string> const& eventKey)
		{
			{
				castl::shared_lock<castl::shared_mutex> lock(m_Mutex);
				auto found = m_EventHandles.find(eventKey);
				if (found != m_EventHandles.end())
				{
					return found->second;
				}
			}
			{
				castl::unique_lock<castl::shared_mutex> lock(m_Mutex);
				auto found = m_EventHandles.find(eventKey);
				if (found != m_EventHandles.end())
				{
					return found->second;
				}
				EventHandle newHandle{};
				newHandle.handleID = m_EventHandles.size();
				found = m_EventHandles.insert(castl::make_pair(eventKey, newHandle)).first;
				found->second.name = found->first.Get();
				return found->second;
			}
		}
		castl::shared_mutex m_Mutex;
		castl::unordered_map<cacore::HashObj<castl::string>, EventHandle> m_EventHandles;
	};

	class TimerSystem_Impl : public TimerSystem
	{
	public:
		// 通过 TimerSystem 继承
		void SetThreadName(const char* pName) override
		{
			ThreadLocalStorage::Get().SetName(pName);
		}

		void BeginEvent(const char* pName, const char* pFilePath, uint32_t lineNumber) override
		{
			auto& eventHandle = m_EventHandlePool.GetOrCreateEventHandle(castl::string{ pName });
			ThreadLocalStorage::Get().BeginEvent(eventHandle);
		}

		void EndEvent() override
		{
			ThreadLocalStorage::Get().EndEvent(m_FrameHistories, m_CurrentCount);
		}
		
		void NewFrame() override
		{
			++m_CurrentCount;
		}

		void CheckInitializeThread()
		{
			auto& tls = ThreadLocalStorage::Get();
			if (tls.m_ThreadID >= 0)
				return;
		}
	private:
		castl::atomic<uint32_t> m_CurrentCount = 0;

		TimerFrameHistories m_FrameHistories;

		EventHandlePool m_EventHandlePool;
	};

}