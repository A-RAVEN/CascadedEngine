#include <stdio.h>
#include <stdint.h>

int Vsnprintf8(char* p, size_t n, const char* pFormat, va_list arguments)
{
    return 0;
    //#ifdef _MSC_VER
    //    return vsnprintf_s(p, n, _TRUNCATE, pFormat, arguments);
    //#else
    //    return vsnprintf(p, n, pFormat, arguments);
    //#endif
}

int Vsnprintf8(char8_t* p, size_t n, const char8_t* pFormat, va_list arguments)
{
    return 0;
    //#ifdef _MSC_VER
    //    return vsnprintf_s(p, n, _TRUNCATE, pFormat, arguments);
    //#else
    //    return vsnprintf(p, n, pFormat, arguments);
    //#endif
}

int Vsnprintf16(char16_t* p, size_t n, const char16_t* pFormat, va_list arguments)
{
    return 0;
    //#ifdef _MSC_VER
    //    return vsnwprintf_s(p, n, _TRUNCATE, pFormat, arguments);
    //#else
    //    return vsnwprintf(p, n, pFormat, arguments); // Won't work on Unix because its libraries implement wchar_t as int32_t.
    //#endif
}

void* __cdecl operator new[](size_t size, const char* name, int flags, unsigned debugFlags, const char* file, int line)
{
    return new uint8_t[size];
}
void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* name, int flags, unsigned debugFlags, const char* file, int line)
{
    return new uint8_t[size];
}