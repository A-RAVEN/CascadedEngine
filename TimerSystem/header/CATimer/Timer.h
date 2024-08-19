#pragma once
#include <stdint.h>

namespace catimer
{
	class TimerSystem
	{
	public:
		virtual void SetThreadName(const char* pName) = 0;
		virtual void BeginEvent(const char* pName, const char* pFilePath = nullptr, uint32_t lineNumber = 0) = 0;
		virtual void EndEvent(const char* pName) = 0;
		virtual void NewFrame() = 0;
	};
	void SetGlobalTimerSystem(TimerSystem* pTimerSystem);
	TimerSystem* GetGlobalTimerSystem();
	struct CPUTimerScope
	{
		const char* cacheName;
		CPUTimerScope(const char* pFunctionName, const char* pFilePath, uint32_t lineNumber, const char* pName)
		{
			cacheName = pName;
			GetGlobalTimerSystem()->BeginEvent(pName, pFilePath, lineNumber);
		}

		CPUTimerScope(const char* pFunctionName, const char* pFilePath, uint32_t lineNumber)
		{
			cacheName = pFunctionName;
			GetGlobalTimerSystem()->BeginEvent(pFunctionName, pFilePath, lineNumber);
		}

		~CPUTimerScope()
		{
			GetGlobalTimerSystem()->EndEvent(cacheName);
		}

		CPUTimerScope(const CPUTimerScope&) = delete;
		CPUTimerScope& operator=(const CPUTimerScope&) = delete;
	};
}
#define MACRO_CONCAT_IMPL(x, y) x##y
#define CPUTIMER_SCOPE(...)				catimer::CPUTimerScope MACRO_CONCAT_IMPL(profiler, __COUNTER__)(__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#define TIMER_NEWFRAME()				catimer::GetGlobalTimerSystem()->NewFrame()