#pragma once
#include <cstdarg>
#define EASTL_EASTDC_VSNPRINTF 0
#define EASTL_LIST_SIZE_CACHE 1
extern int Vsnprintf8(char* p, size_t n, const char* pFormat, va_list arguments);
extern int Vsnprintf8(char8_t* p, size_t n, const char8_t* pFormat, va_list arguments);
extern int Vsnprintf16(char16_t* p, size_t n, const char16_t* pFormat, va_list arguments);
extern void* operator new[](size_t size, const char* name, int flags, unsigned debugFlags, const char* file, int line);
extern void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* name, int flags, unsigned debugFlags, const char* file, int line);
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

namespace castl
{
	using namespace eastl;
}