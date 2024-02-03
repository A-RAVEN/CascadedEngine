#pragma once
#include "ResourceImporter.h"
#include "IResource.h"
#include <ThreadManager.h>

namespace resource_management
{
	class ResourceManagingSystem
	{
	public:
		virtual void* AllocResourceMemory(
			std::string type_name
			, std::string const& resource_path
			, uint64_t size_in_bytes) = 0;
		virtual void ReleaseResourceMemory(std::string type_name, void* pointer) = 0;

		virtual void SerializeAllResources() = 0;

		virtual void SetResourceRootPath(std::string const& path) = 0;

		virtual std::string SetResourceRootPath() const = 0;

		virtual IResource* TryGetResource(std::string const& path) = 0;

		virtual std::vector<std::byte> LoadBinaryFile(std::string const& path) = 0;

		template<typename TRes>
		void LoadResource(std::string const& path, std::function<void(TRes*)> callback)
		{
			static_assert(std::is_base_of<IResource, TRes>::value, "Type T not derived from IResource");
			IResource* result = TryGetResource(path);
			if (result != nullptr)
			{
				callback(static_cast<TRes*>(result));
			}
			else
			{
				auto data = LoadBinaryFile(path);
				TRes* newResult = AllocResource<TRes>(path);
				newResult->Deserialzie(data);
				callback(newResult);
			}
		}

		template<typename TRes, typename...TArgs>
		TRes* AllocResource(std::filesystem::path const& outPath, TArgs&&...Args) {
			static_assert(std::is_base_of<IResource, TRes>::value, "Type T not derived from IResource");
			uint64_t allocSize = sizeof(TRes);
			auto address = AllocResourceMemory(typeid(TRes).name(), outPath.string(), allocSize);
			TRes* result = new (address) TRes(std::forward<TArgs>(Args)...);
			return result;
		}

		template<typename TRes, typename...TArgs>
		TRes* AllocSubResource(std::filesystem::path const& outPath, std::string const& postfix, TArgs&&...Args) {
			static_assert(std::is_base_of<IResource, TRes>::value, "Type T not derived from IResource");
			uint64_t allocSize = sizeof(TRes);
			auto address = AllocResourceMemory(typeid(TRes).name(), (outPath / postfix).string(), allocSize);
			TRes* result =  new (address) TRes(std::forward<TArgs>(Args)...);
			return result;
		}

		template<typename TRes>
		void ReleaseResource(TRes* releasingRes)
		{
			static_assert(std::is_base_of<IResource, TRes>::value, "Type T not derived from IResource");
			ReleaseResourceMemory(typeid(TRes).name(), releasingRes);
		}
	};
}