#pragma once
#if defined(_WIN32) || defined(_WIN64)
#define CA_PLATFORM_WINDOWS 1
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#define NOMINMAX
#include <windows.h>
#pragma warning(disable: 4819)
#pragma warning(disable: 4267)
#endif