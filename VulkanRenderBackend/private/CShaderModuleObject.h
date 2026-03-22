#pragma once
#include <ShaderProvider.h>
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
		cacore::NameHash const& GetEntryPointName() const { return m_EntryPointName; }
	private:
		vk::ShaderModule m_ShaderModule = nullptr;
		cacore::NameHash m_EntryPointName;
	};

	using ShaderModuleObjectDic = HashPool<ShaderSourceInfo, CShaderModuleObject>;
}
