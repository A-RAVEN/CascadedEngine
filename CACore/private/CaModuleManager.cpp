#include <CACore/CAModuleManager.h>
#include <CACore/CAModuleImplementation.h>

namespace cacore
{
	CAModule::CAModule(cacore::PathHash const& modulePath)
	{
		hModuleLib = LoadLibrary(modulePath.c_str());
		if (hModuleLib != nullptr)
		{
			pTryInitFunc = reinterpret_cast<FPT_ModuleFunc>(GetProcAddress(hModuleLib, "TryInit"));
			pTryLinkFunc = reinterpret_cast<FPT_ModuleFunc>(GetProcAddress(hModuleLib, "TryLink"));
			pTryShutDownFunc = reinterpret_cast<FPT_ModuleFunc>(GetProcAddress(hModuleLib, "TryRelease"));
		}
		else
		{
			int errCode = GetLastError();
			CA_LOG_ERR("Load Module Error: {}", errCode);
		}
	}
	void CAModule::TryInit(IModuleManager* pManager)
	{
		if (pTryInitFunc != nullptr)
		{
			pTryInitFunc(pManager);
		}
	}
	void CAModule::TryLink(IModuleManager* pManager)
	{
		if (pTryLinkFunc != nullptr)
		{
			pTryLinkFunc(pManager);
		}
	}

	void CAModule::Shutdown(IModuleManager* pManager)
	{
		if (pTryShutDownFunc != nullptr)
		{
			pTryShutDownFunc(pManager);
		}
		pTryInitFunc = nullptr;
		pTryLinkFunc = nullptr;
		pTryShutDownFunc = nullptr;
		if (hModuleLib != nullptr)
		{
			FreeLibrary(hModuleLib);
			hModuleLib = nullptr;
		}
	}


	CAModuleManager::CAModuleManager()
	{
		//TryInit(this);
	}

	CAModuleManager::~CAModuleManager()
	{
		for (int i = 0; i < m_Factories.size(); ++i)
		{
			m_Factories[i]->ReleaseModuleInstance(m_FactoryInstances[i]);
		}
		m_Factories.clear();
		m_FactoryInstances.clear();

		// 实例对象已在上面释放循环中销毁；m_Instances 只是注册表（AddInstance 填充、
		// RemoveInstance 无调用点），不随对象销毁清空。先清空再断言，避免
		// CA_ASSERT_BREAK 恒触发 __debugbreak 导致退出阶段异常终止。
		m_Instances.clear();

		m_Modules.clear([&](auto path, auto mod)
		{
			mod.Shutdown(this);
		});
		//TryRelease(this);
		CA_ASSERT_BREAK(m_Instances.empty(), "instance container is not empty");
	}

	void CAModuleManager::AddModule(cacore::PathHash const& modulePath)
	{
		m_Modules.get_or_create(modulePath, [&](auto const& path)
		{
			auto result = CAModule(path);
			result.TryInit(this);
			return result;
		});
	}
	void CAModuleManager::LinkModules()
	{
		m_FactoryInstances.resize(m_Factories.size());
		for (int i = 0; i < m_Factories.size(); ++i)
		{
			auto pFactory = m_Factories[i];
			void* newInstance = pFactory->NewModuleInstance();
			AddInstance(pFactory->GetInterfaceTypeName(), pFactory->GetInstanceName(), newInstance);
			m_FactoryInstances[i] = newInstance;
		}
		for(int i = 0; i < m_Factories.size(); ++i)
		{
			m_Factories[i]->LinkModuleInstance(this, m_FactoryInstances[i]);
		}
		m_Modules.for_each([&](auto const& path, CAModule& module)
		{
			module.TryLink(this);
		});
	}
	void* CAModuleManager::GetInstance(cacore::NameHash const& typeName)
	{
		auto pList = m_Instances.try_get(typeName);
		if (pList != nullptr && !pList->empty())
		{
			return pList->begin()->second;
		}
		return nullptr;
	}
	void* CAModuleManager::GetInstance(cacore::NameHash const& typeName, cacore::NameHash const& instanceName)
	{
		auto pList = m_Instances.try_get(typeName);
		if (pList != nullptr)
		{
			auto ppInst = pList->try_get(instanceName);
			if (ppInst != nullptr)
			{
				return *ppInst;
			}
		}
		return nullptr;
	}
	void CAModuleManager::AddInstance(cacore::NameHash const& typeName, cacore::NameHash const& instanceName, void* instancePtr)
	{
		CA_LOG("Add Instance: {} - {}:[{}]", typeName, instanceName, instancePtr);
		auto typeList = m_Instances.get_or_create(typeName, [](auto const&)->castl::shared_dic<cacore::NameHash, void*> { return {}; });
		typeList->second.get_or_create(instanceName, [&](auto const&)-> void* {return instancePtr; });
	}
	void CAModuleManager::RemoveInstance(cacore::NameHash const& typeName, cacore::NameHash const& instanceName)
	{
		auto pList = m_Instances.try_get(typeName);
		if (pList != nullptr)
		{
			pList->try_erase(instanceName);
		}
	}

	void CAModuleManager::AddFactory(cacore::IModuleFactory* pFactory)
	{
		CA_LOG("Add Factory: {} - {}", pFactory->GetInterfaceTypeName(), pFactory->GetInstanceName());
		m_Factories.push_back(pFactory);
	}

}