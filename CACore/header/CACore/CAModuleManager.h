#pragma once
#include "Platform.h"
#include <Hasher.h>
#include <CASTL/CAString.h>
#include <CASTL/CASharedPtr.h>
#include "DebugUtils.h"
#include <CACore/CASharedDic.h>
#include <CACore/CATypeHelper.h>

namespace cacore
{
	class IModuleManager;
	class IModuleFactory
	{
	public:
		virtual void* NewModuleInstance() = 0;
		virtual void* ReleaseModuleInstance(void* instance) = 0;
		virtual void LinkModuleInstance(IModuleManager* pManager, void* instance) = 0;
		virtual const char* GetInstanceName() const = 0;
		virtual const char* GetInterfaceTypeName() const = 0;
		virtual ~IModuleFactory() = default;
	};
	class IModuleManager
	{
	public:
		//Called By User & Module instances
		virtual ~IModuleManager() {};
		virtual void AddModule(cacore::PathHash const& modulePath) = 0;
		virtual void LinkModules() = 0;
		virtual void* GetInstance(cacore::NameHash const& typeName) = 0;
		virtual void* GetInstance(cacore::NameHash const& typeName, cacore::NameHash const& instanceName) = 0;
		template<typename T>
		T* GetInstance()
		{
			const cacore::NameHash interfaceName = CAGetTypeName<T>();
			return static_cast<T*>(GetInstance(interfaceName));
		}
		template<typename T>
		T* GetInstance(cacore::NameHash const& instanceName)
		{
			const cacore::NameHash interfaceName = CAGetTypeName<T>();
			return static_cast<T*>(GetInstance(interfaceName, instanceName));
		}

		//Called By Module
		virtual void AddInstance(cacore::NameHash const& typeName, cacore::NameHash const& instanceName, void* instancePtr) = 0;
		virtual void RemoveInstance(cacore::NameHash const& typeName, cacore::NameHash const& instanceName) = 0;
		virtual void AddFactory(cacore::IModuleFactory* pFactory) = 0;

		template<typename T>
		void AddInstance(cacore::NameHash const& instanceName, T* instancePtr)
		{
			const cacore::NameHash interfaceName = CAGetTypeName<T>();
			AddInstance(interfaceName, instanceName, (void*)instancePtr);
		}

		template<typename T>
		void RemoveInstance(cacore::NameHash const& instanceName)
		{
			const cacore::NameHash interfaceName = CAGetTypeName<T>();
			RemoveInstance(interfaceName, instanceName);
		}

	};
	class CAModule
	{
	public:
		CAModule(cacore::PathHash const& modulePath);
		void TryInit(IModuleManager* pManager);
		void TryLink(IModuleManager* pManager);
		void Shutdown(IModuleManager* pManager);
	private:
		typedef void (*FPT_ModuleFunc)(IModuleManager*);
		FPT_ModuleFunc pTryInitFunc = nullptr;
		FPT_ModuleFunc pTryLinkFunc = nullptr;
		FPT_ModuleFunc pTryShutDownFunc = nullptr;
		HINSTANCE hModuleLib = nullptr;
	};

	class CAModuleManager : public IModuleManager
	{
	public:
		CAModuleManager();
		~CAModuleManager() override;
		//Called By User & Module instances
		void AddModule(cacore::PathHash const& modulePath) override;
		void LinkModules() override;
		void* GetInstance(cacore::NameHash const& typeName) override;
		void* GetInstance(cacore::NameHash const& typeName, cacore::NameHash const& instanceName) override;

		//Called By Module

		void AddInstance(cacore::NameHash const& typeName, cacore::NameHash const& instanceName, void* instancePtr) override;
		void RemoveInstance(cacore::NameHash const& typeName, cacore::NameHash const& instanceName) override;
		void AddFactory(cacore::IModuleFactory* pFactory) override;

	private:
		castl::vector<cacore::IModuleFactory*> m_Factories;
		castl::vector<void*> m_FactoryInstances;
		castl::shared_dic<cacore::PathHash, CAModule> m_Modules;
		castl::shared_dic<cacore::NameHash, castl::shared_dic<cacore::NameHash, void*>> m_Instances;
	};

	static void LogModuleManager(bool isStatic, const char* moduleName)
	{
		if (isStatic)
		{
			CA_LOG("Static Add Module [{}]", moduleName);
		}
		else
		{
			CA_LOG("Try Dynamic Add Module [{}]", moduleName);
		}
	}
}

#define MODULE_TOSTRING_IMPL(x) #x
#define MODULE_TOSTRING(x) MODULE_TOSTRING_IMPL(x)

#define CA_ADD_MODULE(ManagerPtr, ModuleName)\
	{if constexpr(MODULE_TOSTRING(STATIC_BUILT_##ModuleName)=="1")\
	{\
		cacore::LogModuleManager(true, #ModuleName);\
		extern void StaticInitModule_##ModuleName(void*);\
		StaticInitModule_##ModuleName((void*)ManagerPtr);\
	}\
	else\
	{\
		cacore::LogModuleManager(false, #ModuleName);\
		ManagerPtr->AddModule(#ModuleName);\
	}}
