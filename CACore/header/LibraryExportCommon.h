#pragma once
#define CA_LIBRARY_API __declspec(dllexport)
#include <CACore/CATypeHelper.h>
#include <library_loader.h>

template<typename T>
concept InterfaceSupportInit = requires (T t, library_loader::ModuleManager* moduleManager)
{
	{ t.Init(moduleManager) } -> std::same_as<void>;
};

template<typename T>
struct InterfaceTraits
{
	constexpr bool supportInit = InterfaceSupportInit<T>;
};

#define CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(TInterface, TImplemented)\
extern "C"\
{\
	CA_LIBRARY_API TInterface* NewModuleInstance()\
	{\
		return new TImplemented();\
	}\
	CA_LIBRARY_API void DeleteModuleInstance(TInterface* instance)\
	{\
		delete static_cast<TImplemented*>(instance);\
	}\
	CA_LIBRARY_API const char* InterfaceTypeName()\
	{\
		return CAGetTypeName<TInterface>();\
	}\
	CA_LIBRARY_API void TryInit(library_loader::ModuleManager* moduleManager, TInterface* instance)\
	{\
		if(InterfaceTraits<TImplemented>::supportInit)\
		{\
			static_cast<TImplemented*>(instance)->Init(moduleManager);\
		}\
	}\
}
