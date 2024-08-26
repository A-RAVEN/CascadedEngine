#pragma once
#include <ShaderProvider.h>
//#include <uhash.h>
#include <unordered_map>
#include <DebugUtils.h>
#include "VulkanApplicationSubobjectBase.h"
#include "HashPool.h"

namespace graphics_backend
{
	class CShaderModuleObject : public VKAppSubObjectBaseNoCopy
	{
	public:
		CShaderModuleObject(CVulkanApplication& application);
		void Create(ShaderSourceInfo const& shaderSourceInfo);
		void Release();
		vk::ShaderModule GetShaderModule() const { return m_ShaderModule; }
		castl::string const& GetEntryPointName() const { return m_EntryPointName; }
	private:
		vk::ShaderModule m_ShaderModule = nullptr;
		castl::string m_EntryPointName;
	};

	using ShaderModuleObjectDic = HashPool<ShaderSourceInfo, CShaderModuleObject>;
}
