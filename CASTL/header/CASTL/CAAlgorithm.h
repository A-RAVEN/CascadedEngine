#pragma once
#include "CAContainerBase.h"
#if USING_EASTL
#include <EASTL/algorithm.h>
#else
#include <algorithm>
#endif

namespace castl
{
	template<typename T>
	T alignto(T n, T alignN) requires std::is_integral<T>::value
	{
		return ((n + alignN - 1) / alignN) * alignN;
	}
}