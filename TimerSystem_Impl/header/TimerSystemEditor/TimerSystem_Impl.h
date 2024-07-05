#pragma once
#include <CASTL/CAVector.h>
#include <CASTL/CADeque.h>
#include <CASTL/CAString.h>
#include <CASTL/CAChrono.h>
#include <Hasher.h>
namespace catimer
{
	using TimerType = castl::chrono::high_resolution_clock;
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
			uint32_t stackDepth;
			TimerType::time_point beginTime;
			TimerType::time_point endTime;
			void GetDataRangeMS(uint64_t& outBegin, uint64_t& outEnd) const
			{
				outBegin = castl::chrono::duration_cast<castl::chrono::milliseconds>(beginTime.time_since_epoch()).count();
				outEnd = castl::chrono::duration_cast<castl::chrono::milliseconds>(endTime.time_since_epoch()).count();
			}
		};

		void AddEvent(EventHandle const& inEvent, uint32_t stackDepth, TimerType::time_point beginTime, TimerType::time_point endTime)
		{
			m_EventRecords.push_back({ inEvent, stackDepth, beginTime, endTime });
		}
		void Clear()
		{
			m_EventRecords.clear();
		}
		castl::vector<EventData> m_EventRecords;
	};

	struct ThreadHistories
	{
		castl::string m_ThreadName;
		castl::vector<FrameData> m_FrameData;
	};
	
	struct TimerData
	{
		uint32_t startFrame;
		castl::vector<TimerType::time_point> frameBeginTimes;
		castl::vector<ThreadHistories> outThreadHistories;
		uint64_t GetFrameBeginMS(uint32_t frameID)
		{
			return castl::chrono::duration_cast<castl::chrono::milliseconds>(frameBeginTimes[frameID].time_since_epoch()).count();
		}
		void GetTimerDataRangeMS(uint64_t& outBegin, uint64_t& outEnd)
		{
			outBegin = castl::chrono::duration_cast<castl::chrono::milliseconds>(frameBeginTimes.begin()->time_since_epoch()).count();
			outEnd = castl::chrono::duration_cast<castl::chrono::milliseconds>(frameBeginTimes.rbegin()->time_since_epoch()).count();
		}
	};

	class TimerSystem_Editor
	{
	public:
		virtual TimerData QueryHistories() = 0;
	};

	TimerSystem_Editor& GetTimerSystemEditor();
}