#pragma once
#include "ResourceImportingSystem.h"
#include "ResourceManagingSystem.h"
#include <CASTL/CASharedPtr.h>

namespace resource_management
{
	class ResourceFactory
	{
	public:
		virtual ResourceImportingSystem* NewImportingSystem() = 0;
		virtual void DeleteImportingSystem(ResourceImportingSystem*) = 0;
		virtual ResourceManagingSystem* NewManagingSystem() = 0;
		virtual void DeleteManagingSystem(ResourceManagingSystem*) = 0;

		castl::shared_ptr<ResourceImportingSystem> NewImportingSystemShared()
		{
			return castl::shared_ptr<ResourceImportingSystem>(NewImportingSystem(), [this](ResourceImportingSystem* ptr) {DeleteImportingSystem(ptr); });
		}

		castl::shared_ptr<ResourceManagingSystem> NewManagingSystemShared()
		{
			return castl::shared_ptr<ResourceManagingSystem>(NewManagingSystem(), [this](ResourceManagingSystem* ptr) {DeleteManagingSystem(ptr); });
		}
	};
}