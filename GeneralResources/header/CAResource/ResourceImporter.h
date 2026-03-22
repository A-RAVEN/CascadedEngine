#pragma once
#include <CASTL/CAString.h>
#include <CASTL/CAFileSystem.h>
#include <CASTL/CAVector.h>

namespace resource_management
{
	class ResourceManagingSystem;
	//Generate Engine Ready Resource From Source File Created In DCC(i.e. fbx models) Or other Tools(i.e. shader source code)
	class ResourceImporterBase
	{
	public:
		virtual castl::string GetResourceType() const { return ""; }
		virtual castl::string GetSourceFilePostfix() const = 0;
		virtual castl::string GetDestFilePostfix() const = 0;
		virtual castl::string GetTags() const = 0;
		virtual uint64_t GetIResourceSizeInByte() const { return 0; }
		virtual void ImportResource(ResourceManagingSystem* resourceManager, castl::string const& resourcePath, castl::string const& outPath) = 0;
	};

	class ResourceImporterFree
	{
	public:
		virtual castl::string GetTags() const = 0;
		virtual void ImportResource(ResourceManagingSystem* resourceManager
			, cafs::path const& sourcePath
			, cafs::path const& destPath) = 0;
	};

	template<typename TRes>
	class ResourceImporter : public ResourceImporterBase
	{
	public:
		virtual uint64_t GetIResourceSizeInByte() const override
		{
			return sizeof(TRes);
		}

		virtual castl::string GetResourceType() const override
		{
			return castl::string{ typeid(TRes).name() };
		}

	protected:
	};
}