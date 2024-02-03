#pragma once
#include <EASTL/functional.h>

#ifdef CASTL_STD_COMPATIBLE
#include <string>
namespace eastl
{
	template<>
	struct hash<std::string>
	{
		size_t operator()(std::string const& str) const { return std::hash<std::string>{}(str); }
	};
}
#endif

namespace castl = eastl;