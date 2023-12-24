#pragma once
#include "ResourceImportingSystem.h"
#include "ResourceManagingSystem.h"
#include <memory>

namespace resource_management
{
	class ResourceFactory
	{
	public:
		virtual ResourceImportingSystem* NewImportingSystem() = 0;
		virtual void DeleteImportingSystem(ResourceImportingSystem*) = 0;
		virtual ResourceManagingSystem* NewManagingSystem() = 0;
		virtual void DeleteManagingSystem(ResourceManagingSystem*) = 0;

		std::shared_ptr<ResourceImportingSystem> NewImportingSystemShared()
		{
			return std::shared_ptr<ResourceImportingSystem>(NewImportingSystem(), [this](ResourceImportingSystem* ptr) {DeleteImportingSystem(ptr); });
		}

		std::shared_ptr<ResourceManagingSystem> NewManagingSystemShared()
		{
			return std::shared_ptr<ResourceManagingSystem>(NewManagingSystem(), [this](ResourceManagingSystem* ptr) {DeleteManagingSystem(ptr); });
		}
	};
}