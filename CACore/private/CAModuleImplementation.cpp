#include <CACore/CAModuleImplementation.h>

//extern "C"
//{
//	CA_MODULE_API void TryInit(void* pManager) {
//		cacore::IModuleManager* mgr = (cacore::IModuleManager*)pManager;
//		for (cacore::IModuleFactory* factory : cacore::GetModuleFactories())
//		{
//			mgr->AddFactory(factory);
//		}
//	};
//
//	CA_MODULE_API void TryLink(void* pManager) {
//	};
//
//	CA_MODULE_API void TryRelease(void* pManager) {
//
//	};
//}