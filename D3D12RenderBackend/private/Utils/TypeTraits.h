#pragma once
#include <CASTL/CATypeTraits.h>

namespace graphics_backend
{
	template<typename ValType, typename DescType>
	concept CanInitWithDesc = requires(DescType desc, ValType val)
	{
		val.Init(desc);
	};

	template<typename T, typename...TArgs>
	concept CanInit = requires(T t, TArgs ... args)
	{
		t.Init(args...);
	};

	template<typename T>
	concept CanRelease = requires(T t)
	{
		t.Release();
	};
}