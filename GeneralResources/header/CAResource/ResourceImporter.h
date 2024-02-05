#pragma once
#include <CASTL/CAString.h>
#include <CASTL/CAVector.h>

namespace resource_management
{
	class ResourceManagingSystem;
	//Generate Engine Ready Resource From Source File Created In DCC(i.e. fbx models) Or other Tools(i.e. shader source code)
	class ResourceImporterBase
	{
	public:
		virtual castl::string GetResourceType() const = 0;
		virtual castl::string GetSourceFilePostfix() const = 0;
		virtual castl::string GetDestFilePostfix() const = 0;
		virtual castl::string GetTags() const = 0;
		virtual uint64_t GetIResourceSizeInByte() const = 0;
		virtual void ImportResource(ResourceManagingSystem* resourceManager, castl::string const& resourcePath, castl::string const& outPath) = 0;
	};

	template<typename TRes>
	class ResourceImporterPass
	{
	public:
		virtual void Process(TRes* resource) const = 0;
	};

	template<typename TRes>
	class ResourceImporter : public ResourceImporterBase
	{
	public:
		virtual uint64_t GetIResourceSizeInByte() const override
		{
			return sizeof(TRes);
		}

		void ApplyPasses(TRes* resource) const
		{
			for (auto pass : m_Passes)
			{
				pass->Process(resource);
			}
		}

		virtual castl::string GetResourceType() const override
		{
			return castl::string{ typeid(TRes).name() };
		}

	protected:
		castl::vector<ResourceImporterPass<TRes> const*> m_Passes;
	};
}