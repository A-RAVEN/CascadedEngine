#pragma once
#include "Platform.h"
#include <Hasher.h>
#include <CASTL/CAString.h>
#include <CASTL/CASharedPtr.h>
#include "DebugUtils.h"
#include <CACore/CASharedDic.h>
#include <CACore/CATypeHelper.h>

namespace library_loader
{


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



	class ModuleManager;
	class Module
	{
	public:
		Module(castl::string const& modulePath);
		bool isValid() const;
		void ReleaseModule();
		void* GetInstance();
		void ReleaseInstance();
		void TryInit(ModuleManager* moduleManager);
		template<typename T>
		T* GetInstance()
		{
			return static_cast<T*>(GetInstance());
		}
		const char* GetInterfaceTypeName() const;
	private:
		typedef void* (*FTP_NewModuleObject)();
		typedef void(*FPT_DeleteModuleObject)(void*);
		typedef char const*(*FPT_GetInterfaceType)();
		typedef void (*FPT_TryInit)(ModuleManager*, void*);
		HINSTANCE hModuleLib = nullptr;
		FTP_NewModuleObject pNewInstanceFunc = nullptr;
		FPT_DeleteModuleObject pDeleteInstanceFunc = nullptr;
		FPT_GetInterfaceType pGetInterfaceTypeName = nullptr;
		FPT_TryInit pTryInitFunc = nullptr;
		void* pInstance = nullptr;
	};

	class ModuleManager
	{
	public:
		~ModuleManager();
		Module& EnsureModule(cacore::NameHash const& moduleName);
		void StartupModules();
		void* GetModuleInstance(const char* interfaceName);
		template<typename T>
		T* GetModuleInstance()
		{
			const char* interfaceName = CAGetTypeName<T>();
			return static_cast<T*>(GetModuleInstance(interfaceName));
		}
	private:
		castl::shared_dic<cacore::NameHash, Module> m_NameToModules;
		castl::shared_dic<cacore::NameHash, castl::vector<cacore::NameHash>> m_InterfaceToModuleName;
	};


}