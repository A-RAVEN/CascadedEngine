#include "MiniDump.h"
#include <stdio.h>


MiniDump::MiniDump()
{
}


MiniDump::~MiniDump()
{
}

void MiniDump::EnableAutoDump(bool bEnable)
{
	if (bEnable)
	{
		SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)ApplicationCrashHandler);
	}
}

LONG MiniDump::ApplicationCrashHandler(EXCEPTION_POINTERS* pException)
{
	// Recursion guard: prevent re-entry if crash handler itself crashes
	static volatile bool s_InCrashHandler = false;
	if (s_InCrashHandler)
		return EXCEPTION_CONTINUE_SEARCH;
	s_InCrashHandler = true;

	HANDLE hStderr = GetStdHandle(STD_ERROR_HANDLE);

	// --- Text stack trace output (WriteFile, not fprintf, to avoid CRT FILE* lock deadlock) ---
	char buffer[4096];

	// Map common exception codes to human-readable names
	const char* exceptionName = "UNKNOWN_EXCEPTION";
	switch (pException->ExceptionRecord->ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:         exceptionName = "ACCESS_VIOLATION"; break;
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    exceptionName = "ARRAY_BOUNDS_EXCEEDED"; break;
	case EXCEPTION_BREAKPOINT:               exceptionName = "BREAKPOINT"; break;
	case EXCEPTION_DATATYPE_MISALIGNMENT:    exceptionName = "DATATYPE_MISALIGNMENT"; break;
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:       exceptionName = "FLT_DIVIDE_BY_ZERO"; break;
	case EXCEPTION_FLT_OVERFLOW:             exceptionName = "FLT_OVERFLOW"; break;
	case EXCEPTION_ILLEGAL_INSTRUCTION:      exceptionName = "ILLEGAL_INSTRUCTION"; break;
	case EXCEPTION_INT_DIVIDE_BY_ZERO:       exceptionName = "INT_DIVIDE_BY_ZERO"; break;
	case EXCEPTION_STACK_OVERFLOW:           exceptionName = "STACK_OVERFLOW"; break;
	}

	int len = snprintf(buffer, sizeof(buffer),
		"\n=== CRASH DETECTED ===\n"
		"Exception   : %s (0x%08X)\n"
		"Address     : 0x%p\n"
		"Thread ID   : %lu\n"
		"Call Stack  :\n",
		exceptionName,
		pException->ExceptionRecord->ExceptionCode,
		pException->ExceptionRecord->ExceptionAddress,
		GetCurrentThreadId());
	if (len > 0)
	{
		DWORD written = 0;
		WriteFile(hStderr, buffer, (DWORD)len, &written, NULL);
	}

	// Resolve the module containing the faulting address — decisive for identifying
	// whether the crash is in our DLL, the Vulkan driver, or the CRT heap.
	{
		void* crashAddr = pException->ExceptionRecord->ExceptionAddress;
		HMODULE hMod = NULL;
		GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)crashAddr, &hMod);
		char modPath[MAX_PATH] = { 0 };
		const char* modName = "unknown";
		if (hMod)
		{
			DWORD nameLen = GetModuleFileNameA(hMod, modPath, MAX_PATH);
			if (nameLen > 0)
			{
				modName = modPath;
				for (DWORD j = nameLen; j > 0; --j)
				{
					if (modPath[j - 1] == '\\' || modPath[j - 1] == '/')
					{
						modName = &modPath[j];
						break;
					}
				}
			}
		}
		if (hMod)
		{
			// hMod == image base for a loaded module; RVA = faulting address - base.
			len = snprintf(buffer, sizeof(buffer), "Module      : %s (base 0x%p, RVA 0x%08X)\n",
				modName, hMod, (unsigned)((char*)crashAddr - (char*)hMod));
		}
		else
		{
			len = snprintf(buffer, sizeof(buffer), "Module      : %s (unresolved)\n", modName);
		}
		if (len > 0)
		{
			DWORD written = 0;
			WriteFile(hStderr, buffer, (DWORD)len, &written, NULL);
		}
	}

	// Capture call stack (up to 16 frames)
	const int maxFrames = 16;
	void* stackFrames[16];
	WORD frameCount = CaptureStackBackTrace(0, maxFrames, stackFrames, NULL);

	for (WORD i = 0; i < frameCount; ++i)
	{
		HMODULE hModule = NULL;
		GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)stackFrames[i],
			&hModule);

		char modulePath[MAX_PATH] = { 0 };
		const char* moduleName = "unknown";
		if (hModule)
		{
			DWORD nameLen = GetModuleFileNameA(hModule, modulePath, MAX_PATH);
			if (nameLen > 0)
			{
				// Extract filename from full path
				moduleName = modulePath;
				for (DWORD j = nameLen; j > 0; --j)
				{
					if (modulePath[j - 1] == '\\' || modulePath[j - 1] == '/')
					{
						moduleName = &modulePath[j];
						break;
					}
				}
			}
		}

		len = snprintf(buffer, sizeof(buffer), "  [%2d] 0x%p - %s\n", i, stackFrames[i], moduleName);
		if (len > 0)
		{
			DWORD written = 0;
			WriteFile(hStderr, buffer, (DWORD)len, &written, NULL);
		}
	}

	// Build dump file path
	TCHAR szDumpDir[MAX_PATH] = { 0 };
	TCHAR szDumpFile[MAX_PATH] = { 0 };
	SYSTEMTIME	stTime = { 0 };
	GetLocalTime(&stTime);
	::GetCurrentDirectory(MAX_PATH, szDumpDir);
	TSprintf(szDumpFile, _T("%s\\crash_%04d%02d%02d_%02d%02d%02d.dmp"), szDumpDir,
		stTime.wYear, stTime.wMonth, stTime.wDay,
		stTime.wHour, stTime.wMinute, stTime.wSecond);

	// Output dump file path
	len = snprintf(buffer, sizeof(buffer), "\nDump file  : %s\n", szDumpFile);
	if (len > 0)
	{
		DWORD written = 0;
		WriteFile(hStderr, buffer, (DWORD)len, &written, NULL);
	}

	// Create dump file
	CreateDumpFile(szDumpFile, pException);

	// Terminate process immediately (no message box — headless/CI safe)
	// Flush buffered stdout/stderr before exit to preserve diagnostic output
	fflush(stdout);
	fflush(stderr);
	TerminateProcess(GetCurrentProcess(), 1);

	return EXCEPTION_EXECUTE_HANDLER;
}

void MiniDump::CreateDumpFile(PATHTYPE strPath, EXCEPTION_POINTERS* pException)
{
	// Create Dump file
	HANDLE hDumpFile = CreateFile(strPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDumpFile == INVALID_HANDLE_VALUE)
		return;

	// Dump info
	MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
	dumpInfo.ExceptionPointers = pException;
	dumpInfo.ThreadId = GetCurrentThreadId();
	dumpInfo.ClientPointers = TRUE;

	// Write Dump file content
	MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpNormal, &dumpInfo, NULL, NULL);
	CloseHandle(hDumpFile);
}
