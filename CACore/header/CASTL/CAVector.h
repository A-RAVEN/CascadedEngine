#pragma once
#include "CAContainerBase.h"
#include <EASTL/vector.h>


#include <vector>
namespace castl
{
	template <typename T>
	inline std::vector<T> to_std(vector<T> const& vec)
	{
		std::vector<T> result;
		result.resize(vec.size());
		for (size_t i = 0; i < vec.size(); ++i)
			result[i] = vec[i];
		return result;
	}

	template <typename T>
	inline vector<T> to_ca(std::vector<T> const& vec)
	{
		vector<T> result;
		result.resize(vec.size());
		for(size_t i = 0; i < vec.size(); ++i)
			result[i] = vec[i];
		return result;
	}
}
