#pragma once
#include "ResourceImporter.h"
#include <ThreadManager.h>

namespace resource_management
{
	class ResourceManagingSystem;
	class ResourceImportingSystem
	{
	public:
		virtual void SetResourceManager(ResourceManagingSystem* resourceManagingSystem) = 0;
		// 唯一导入器形态：目录级、拉取产出、不注册（旧的 1:1 后缀导入器抽象已退役）。
		virtual void AddImporter(ResourceImporter* importer) = 0;
		virtual void ScanSourceDirectory(const castl::string& sourceDirectory) = 0;
	};
}
