#pragma once
#include "CAContainerBase.h"
#include <EASTL/string.h>

namespace castl
{
	inline std::string to_std(string const& str)
	{
		return std::string(str.c_str());
	}
}

