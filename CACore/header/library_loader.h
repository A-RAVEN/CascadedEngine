#pragma once
#include "Platform.h"
#include "CASTL/CAString.h"
#include "CASTL/CaSharedPtr.h"
#include "DebugUtils.h"

namespace library_loader
{
	//class IModuleLoader
	//{
	//public:
	//	virtual ~IModuleLoader() {}
	//};

	template<typename TModInstance>
	class TModuleLoader// : public IModuleLoader
	{
	private:
		typedef TModInstance* (*FTP_NewModuleObject)();
		typedef void(*FPT_DeleteModuleObject)(TModInstance*);

		HINSTANCE hModuleLib = nullptr;
		FTP_NewModuleObject pNewInstanceFunc = nullptr;
		FPT_DeleteModuleObject pDeleteInstanceFunc = nullptr;
	public:
		TModuleLoader(
#if UNICODE
			wchar_t const* modulePath
#else
			char const* modulePath
#endif
		)
		{
			hModuleLib = LoadLibrary(modulePath);
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

		~TModuleLoader()
		{
			pNewInstanceFunc = nullptr;
			pDeleteInstanceFunc = nullptr;
			if (hModuleLib != nullptr)
			{
				FreeLibrary(hModuleLib);
				hModuleLib = nullptr;
			}
		}

		castl::shared_ptr<TModInstance> New()
		{
			return castl::shared_ptr<TModInstance>(NewModuleInstance(), [this](TModInstance* removingInstance) { DeleteModuleInstance(removingInstance); });
		}

		TModInstance* NewModuleInstance()
		{
			return pNewInstanceFunc();
		}

		void DeleteModuleInstance(TModInstance* moduleObject)
		{
			if (pDeleteInstanceFunc != nullptr)
			{
				pDeleteInstanceFunc(moduleObject);
			}
		}
	};
}