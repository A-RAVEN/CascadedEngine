#include <ShaderResourceHandle.h>

namespace graphics_backend
{
	static castl::atomic<uint32_t> s_ThreadCounter;
	thread_local static bool s_Initialized;
	thread_local static uint32_t s_ThreadID;
	thread_local static uint32_t s_NextID;

	static void InitCounter()
	{
		if (!s_Initialized)
		{
			s_Initialized = true;
			s_ThreadID = s_ThreadCounter++;
			s_NextID = 0;
		}
	}

	uint32_t ThreadID::Get()
	{
		InitCounter();
		return s_ThreadID;
	}

	uint32_t ThreadLocalID::Get()
	{
		InitCounter();
		return s_NextID++;
	}

}