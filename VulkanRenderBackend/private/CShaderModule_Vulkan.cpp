#include "pch.h"
#include <ShaderProvider.h>
#include "CShaderModuleObject.h"
#include "VulkanApplication.h"

namespace graphics_backend
{
	CShaderModuleObject::CShaderModuleObject(CVulkanApplication& application)
	: VKAppSubObjectBaseNoCopy(application)
	{}

	void CShaderModuleObject::Create(ShaderSourceInfo const& shaderSourceInfo)
	{
		m_EntryPointName = shaderSourceInfo.entryPoint;
		vk::ShaderModuleCreateInfo shaderModuelCreateInfo{
			vk::ShaderModuleCreateFlags{}
			, shaderSourceInfo.dataLength
			, static_cast<uint32_t const*>(shaderSourceInfo.dataPtr)
			, nullptr
		};
		m_ShaderModule = GetDevice().createShaderModule(shaderModuelCreateInfo);
	}
	void CShaderModuleObject::Release()
	{
		if (m_ShaderModule != vk::ShaderModule(nullptr))
		{
			GetDevice().destroyShaderModule(m_ShaderModule);
			m_ShaderModule = nullptr;
		}
	}
}

