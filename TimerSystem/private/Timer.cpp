#include <CATimer/Timer.h>
namespace catimer
{
	TimerSystem* gCPUTimer = nullptr;
	void SetGlobalTimerSystem(TimerSystem* pTimerSystem)
	{
		gCPUTimer = pTimerSystem;
	}
	TimerSystem* GetGlobalTimerSystem()
	{
		return gCPUTimer;
	}
}