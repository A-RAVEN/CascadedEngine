#pragma once
#include "CAContainerBase.h"
#include <EASTL/string.h>

#include <string>
namespace castl
{
	inline std::string to_std(castl::string const& str)
	{
		return std::string(str.c_str());
	}

	inline castl::string to_ca(std::string const& str)
	{
		return castl::string(str.c_str());
	}
}

