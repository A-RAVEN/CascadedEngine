#pragma once
#include "CAContainerBase.h"
#if USING_EASTL
#include <EASTL/vector.h>
#include <vector>
#else
#include <vector>
#endif

namespace castl
{
	template <typename T>
	inline std::vector<T> to_std(vector<T> const& vec)
	{
#if USING_EASTL
		std::vector<T> result;
		result.resize(vec.size());
		for (size_t i = 0; i < vec.size(); ++i)
			result[i] = vec[i];
		return result;
#else
		return vec;
#endif
	}

	template <typename T>
	inline vector<T> to_ca(std::vector<T> const& vec)
	{
#if USING_EASTL
		vector<T> result;
		result.resize(vec.size());
		for(size_t i = 0; i < vec.size(); ++i)
			result[i] = vec[i];
		return result;
#else
		return vec;
#endif
	}
}
