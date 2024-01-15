#pragma once
#include "VulkanApplicationSubobjectBase.h"
#include "RenderInterface/header/ShaderProvider.h"
#include <CACore/header/uhash.h>
#include <unordered_map>
#include "HashPool.h"

namespace graphics_backend
{
	struct ShaderModuleDescritor
	{
		ShaderProvider const* provider;

		bool operator==(ShaderModuleDescritor const& other) const
		{
			if(provider == nullptr || other.provider == nullptr)
				return false;
			else if(provider == other.provider)
				return true;
			else if(provider->GetUniqueName() == other.provider->GetUniqueName())
				return true;
		}

		template <class HashAlgorithm>
		friend void hash_append(HashAlgorithm& h, ShaderModuleDescritor const& shadermodule_desc) noexcept
		{
			hash_append(h, shadermodule_desc.provider);
		}
	};

	class CShaderModuleObject : public BaseApplicationSubobject
	{
	public:
		CShaderModuleObject(CVulkanApplication& application);
		void Create(ShaderModuleDescritor const& descriptor);
		virtual void Release() override;
		vk::ShaderModule GetShaderModule() const { return m_ShaderModule; }
		std::string const& GetEntryPointName() const { return m_EntryPointName; }
	private:
		vk::ShaderModule m_ShaderModule = nullptr;
		std::string m_EntryPointName;
	};

	using ShaderModuleObjectDic = HashPool<ShaderModuleDescritor, CShaderModuleObject>;
}
