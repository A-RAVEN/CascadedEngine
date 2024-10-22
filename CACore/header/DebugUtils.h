#pragma once
#include <assert.h>
#include <CASTL/CAString.h>
#include <iostream>
#include <stdio.h>
#include <stdint.h>
#ifdef _WIN32
#include <Windows.h>
#include <dbghelp.h>
#include <shellapi.h>
#include <shlobj.h>
#endif // _WIN32


#define CA_ASSERT( _condition , _log ) {if(!(_condition)){CALogError(_log, __LINE__, __FILE__);}}
#define CA_ASSERT_BREAK( _condition , _log ) {if(!(_condition)){CALogError(_log, __LINE__, __FILE__, true);}}
#define CA_LOG_ERR(_log) {CALogError(_log, __LINE__, __FILE__);}
#define CA_CLASS_NAME(_class) (typeid(_class).name())

static inline void CALogError(castl::string const& log, int line, castl::string const& file, bool debugBreak = false)
{
	if (debugBreak)
	{
		__debugbreak();
	}
#ifdef _WIN32
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
#endif
	castl::string out = "\n" + log + "\nLine: " + castl::to_string(line) + "\nFile: " + file + "\n";
	std::cerr << castl::to_std(out) << std::endl;
#ifdef _WIN32
	SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN);
#endif
}