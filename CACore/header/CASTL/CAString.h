#pragma once
#include "CAContainerBase.h"
#if USING_EASTL
#include <EASTL/string.h>
#include <string>
#include <ostream>
#else
#include <string>
#endif
namespace castl
{
//#if USING_EASTL
//	std::ostream& operator<<(std::ostream& os, const castl::string& dt)
//	{
//		os << dt.c_str();
//		return os;
//	}
//#endif

	inline std::string to_std(castl::string const& str)
	{
#if USING_EASTL
		return std::string(str.c_str());
#else
		return str;
#endif
	}

	inline castl::string to_ca(std::string const& str)
	{
#if USING_EASTL
		return castl::string(str.c_str());
#else
		return str;
#endif
	}
}

