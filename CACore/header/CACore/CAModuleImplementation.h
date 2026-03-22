#pragma once
#include <CACore/CAModuleManager.h>
#include <CASTL/CAVector.h>
#define CA_MODULE_API __declspec(dllexport)

#ifndef CA_MODULE_NAME
#define CA_MODULE_NAME DefaultModule
#endif
#ifndef CA_STATIC_BUILD
#define CA_STATIC_BUILD 1
#endif

#define CA_PASTE(a,b) a##b
#define CA_EXPAND_PASTE(a,b) CA_PASTE(a,b)

namespace cacore
{

	castl::vector<IModuleFactory*>& CA_EXPAND_PASTE(GetModuleFactories_, CA_MODULE_NAME)();

	template<typename T>
	concept linkable = requires(T& t, IModuleManager* pManager)
	{
		t.Init(pManager);
	};

	template<typename TI, typename T>
	class ModuleFactory : public IModuleFactory
	{
	public:
		ModuleFactory(const char* instanceName) : m_InstanceName(instanceName)
		{
			CA_LOG("Register Module Factory: {} - {}", CAGetTypeName<TI>(), instanceName);
			CA_EXPAND_PASTE(GetModuleFactories_, CA_MODULE_NAME)().push_back(this);
		}

		const char* GetInterfaceTypeName() const override
		{
			return CAGetTypeName<TI>();
		}

		const char* GetInstanceName() const override
		{
			return m_InstanceName.c_str();
		}
		void* NewModuleInstance() override
		{
			return static_cast<TI*>(new T());
		}
		void* ReleaseModuleInstance(void* instance) override
		{
			delete static_cast<T*>(static_cast<TI*>(instance));
			return nullptr;
		}

		virtual void LinkModuleInstance(IModuleManager* pManager, void* instance) override
		{
			if constexpr (linkable<T>)
			{
				CA_LOG("{} supports link", m_InstanceName);
				static_cast<T*>(static_cast<TI*>(instance))->Init(pManager);
			}
			else
			{
				CA_LOG("{} not supports link", m_InstanceName);
			}
		}

	private:
		castl::string m_InstanceName;
	};
}

#define CA_MODULE_INSTANCE(TInterface, TImplemented, InstanceName)\
static volatile cacore::ModuleFactory<TInterface, TImplemented> g_ModuleFactory_##TInterface_##TImplemented_##InstanceName{#InstanceName};\

#ifdef CA_IMPLEMENT_MODULE
namespace cacore
{
	castl::vector<IModuleFactory*>& CA_EXPAND_PASTE(GetModuleFactories_, CA_MODULE_NAME)()
	{
		static castl::vector<IModuleFactory*> CA_EXPAND_PASTE(s_ModuleLocalFactories_, CA_MODULE_NAME) {};
		return CA_EXPAND_PASTE(s_ModuleLocalFactories_, CA_MODULE_NAME);
	}
}

#if CA_STATIC_BUILD
void CA_EXPAND_PASTE(StaticInitModule_, CA_MODULE_NAME)(void* pManager)
{
	cacore::IModuleManager* mgr = (cacore::IModuleManager*)pManager;
	for (cacore::IModuleFactory* factory : cacore::CA_EXPAND_PASTE(GetModuleFactories_, CA_MODULE_NAME)())
	{
		mgr->AddFactory(factory);
	}
}
#else
extern "C"
{
	CA_MODULE_API void TryInit(void* pManager) {
		cacore::IModuleManager* mgr = (cacore::IModuleManager*)pManager;
		for (cacore::IModuleFactory* factory : cacore::CA_EXPAND_PASTE(GetModuleFactories_, CA_MODULE_NAME)())
		{
			mgr->AddFactory(factory);
		}
	};
	//CA_MODULE_API void TryLink(void* pManager) {
	//};

	//CA_MODULE_API void TryRelease(void* pManager) {

	//};
}
#endif
#endif


//extern "C"
//{
//#pragma comment(linker, "/INCLUDE:TryInit")
//#pragma comment(linker, "/INCLUDE:TryLink")
//#pragma comment(linker, "/INCLUDE:TryRelease")
//
//	CA_MODULE_API void TryInit(void* pManager);
//	CA_MODULE_API void TryLink(void* pManager);
//	CA_MODULE_API void TryRelease(void* pManager);
//
//}
