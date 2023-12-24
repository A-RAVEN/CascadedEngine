#pragma once
#include <string>
#include <vector>

namespace resource_management
{
	class ResourceManagingSystem;
	//Generate Engine Ready Resource From Source File Created In DCC(i.e. fbx models) Or other Tools(i.e. shader source code)
	class ResourceImporterBase
	{
	public:
		virtual std::string GetResourceType() const = 0;
		virtual std::string GetSourceFilePostfix() const = 0;
		virtual std::string GetDestFilePostfix() const = 0;
		virtual std::string GetTags() const = 0;
		virtual uint64_t GetIResourceSizeInByte() const = 0;
		virtual void ImportResource(ResourceManagingSystem* resourceManager, std::string const& resourcePath, std::string const& outPath) = 0;
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

		virtual std::string GetResourceType() const override
		{
			return std::string{ typeid(TRes).name() };
		}

	protected:
		std::vector<ResourceImporterPass<TRes> const*> m_Passes;
	};
}