#pragma once
#include "ResourceImporter.h"
#include <ThreadManager/header/ThreadManager.h>

namespace resource_management
{
	class ResourceImportingSystem
	{
	public:
		virtual void AddImporter(ResourceImporterBase* importer) = 0;
		virtual void ScanSourceDirectory(const std::string& sourceDirectory) = 0;
	};
}