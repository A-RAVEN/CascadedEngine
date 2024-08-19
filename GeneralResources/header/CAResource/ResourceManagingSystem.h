#pragma once
#include "ResourceImporter.h"
#include "IResource.h"
#include <ThreadManager.h>
#include <functional>

namespace resource_management
{
	class ResourceManagingSystem
	{
	public:
		virtual void* AllocResourceMemory(
			castl::string type_name
			, castl::string const& resource_path
			, uint64_t size_in_bytes) = 0;
		virtual void ReleaseResourceMemory(castl::string type_name, void* pointer) = 0;

		virtual void SerializeAllResources() = 0;

		virtual void SetResourceRootPath(castl::string const& path) = 0;

		virtual castl::string GetResourceRootPath() const = 0;

		virtual IResource* TryGetResource(castl::string const& path) = 0;

		virtual castl::vector<uint8_t> LoadBinaryFile(castl::string const& path) = 0;

		template<typename TRes>
		void LoadResource(castl::string const& path, std::function<void(TRes*)> callback)
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
		TRes* AllocResource(castl::string const& outPath, TArgs&&...Args) {
			static_assert(std::is_base_of<IResource, TRes>::value, "Type T not derived from IResource");
			uint64_t allocSize = sizeof(TRes);
			auto address = AllocResourceMemory(typeid(TRes).name(), outPath, allocSize);
			TRes* result = new (address) TRes(std::forward<TArgs>(Args)...);
			return result;
		}

		template<typename TRes, typename...TArgs>
		TRes* AllocSubResource(castl::string const& outPath, castl::string const& postfix, TArgs&&...Args) {
			static_assert(std::is_base_of<IResource, TRes>::value, "Type T not derived from IResource");
			uint64_t allocSize = sizeof(TRes);
			auto address = AllocResourceMemory(typeid(TRes).name(), (outPath + "/" + postfix), allocSize);
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