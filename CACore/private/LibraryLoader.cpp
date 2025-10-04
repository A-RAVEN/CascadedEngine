#include <library_loader.h>

namespace library_loader
{
	Module::Module(castl::string const& modulePath)
	{
		hModuleLib = LoadLibrary(modulePath.c_str());
		if (hModuleLib != nullptr)
		{
			pNewInstanceFunc = reinterpret_cast<FTP_NewModuleObject>(GetProcAddress(hModuleLib, "NewModuleInstance"));
			pDeleteInstanceFunc = reinterpret_cast<FPT_DeleteModuleObject>(GetProcAddress(hModuleLib, "DeleteModuleInstance"));
		}
		else
		{
			int errCode = GetLastError();
			CA_LOG_ERR("Load Module Error: {}", errCode);
		}
	}

	void* Module::NewInstance()
	{
		if (pNewInstanceFunc)
		{
			return pNewInstanceFunc();
		}
		return nullptr;
	}


	void Module::DeleteInstance(void* ptr)
	{
		if (pDeleteInstanceFunc)
		{
			pDeleteInstanceFunc(ptr);
		}
	}

}