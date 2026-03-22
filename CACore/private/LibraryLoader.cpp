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
			pGetInterfaceTypeName = reinterpret_cast<FPT_GetInterfaceType>(GetProcAddress(hModuleLib, "InterfaceTypeName"));
			pTryInitFunc = reinterpret_cast<FPT_TryInit>(GetProcAddress(hModuleLib, "TryInit"));
		}
		else
		{
			int errCode = GetLastError();
			CA_LOG_ERR("Load Module Error: {}", errCode);
		}
	}

	bool Module::isValid() const
	{
		return hModuleLib != nullptr
			&& pNewInstanceFunc != nullptr
			&& pDeleteInstanceFunc != nullptr
			&& pTryInitFunc != nullptr
			&& pGetInterfaceTypeName != nullptr;
	}

	void Module::ReleaseModule()
	{
		ReleaseInstance();
		pNewInstanceFunc = nullptr;
		pDeleteInstanceFunc = nullptr;
		pGetInterfaceTypeName = nullptr;
		pTryInitFunc = nullptr;
		if (hModuleLib != nullptr)
		{
			FreeLibrary(hModuleLib);
			hModuleLib = nullptr;
		}
	}

	void* Module::GetInstance()
	{
		if (pInstance == nullptr)
		{
			if (pNewInstanceFunc)
			{
				pInstance = pNewInstanceFunc();
			}
		}
		return pInstance;
	}
	void Module::ReleaseInstance()
	{
		if (pInstance != nullptr)
		{
			if (pDeleteInstanceFunc)
			{
				pDeleteInstanceFunc(pInstance);
			}
			pInstance = nullptr;
		}
	}

	void Module::TryInit(library_loader::ModuleManager* moduleManager)
	{
		if (pTryInitFunc != nullptr && pInstance != nullptr)
		{
			pTryInitFunc(moduleManager, pInstance);
		}
	}

	const char* Module::GetInterfaceTypeName() const
	{
		if (pGetInterfaceTypeName != nullptr)
		{
			return pGetInterfaceTypeName();
		}
		return nullptr;
	}

	Module& ModuleManager::EnsureModule(cacore::NameHash const& moduleName)
	{
		return m_NameToModules.get_or_create(moduleName, [&](cacore::NameHash const& moduleName) -> Module
		{
			Module newModule(moduleName.string());
			CA_ASSERT_BREAK(newModule.isValid(), "Load Module {} Failed!", moduleName.string());
			cacore::NameHash interfaceTypeName(newModule.GetInterfaceTypeName());
			CA_LOG("Load Module {} for Interface {}", moduleName.string(), interfaceTypeName.string());
			m_InterfaceToModuleName.get_or_create(interfaceTypeName, [&](cacore::NameHash const&)->castl::vector<cacore::NameHash>
			{
				return castl::vector<cacore::NameHash>{ };
			})->second.push_back(moduleName);
			newModule.GetInstance();
			return newModule;
		})->second;
	}
	void ModuleManager::StartupModules()
	{
		m_NameToModules.for_each([&](auto name, Module& mod)
		{
			mod.TryInit(this);
		});
	}
	ModuleManager::~ModuleManager()
	{
		m_NameToModules.clear([&](auto name, Module& mod)
		{
			mod.ReleaseModule();
		});
	}
	void* ModuleManager::GetModuleInstance(const char* interfaceName)
	{
		CA_LOG("Try Get Interface {}", interfaceName);
		cacore::NameHash interfaceTypeName(interfaceName);
		auto nameVector = m_InterfaceToModuleName.try_get(interfaceTypeName);
		if (nameVector != nullptr && !nameVector->empty())
		{
			auto pModule = m_NameToModules.try_get(nameVector->front());
			if (pModule != nullptr)
			{
				CA_LOG("Found Instance Named {} for Interface {}", nameVector->front(), interfaceName);
				return pModule->GetInstance();
			}
		}
		return nullptr;
	}

}