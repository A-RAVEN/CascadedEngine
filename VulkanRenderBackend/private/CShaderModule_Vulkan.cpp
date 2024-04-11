#include "pch.h"
#include <ShaderProvider.h>
#include "CShaderModuleObject.h"
#include "VulkanApplication.h"

namespace graphics_backend
{
	CShaderModuleObject::CShaderModuleObject(CVulkanApplication& application)
	: VKAppSubObjectBaseNoCopy(application)
	{}

	void CShaderModuleObject::Create(ShaderModuleDescritor const& descriptor)
	{
		ShaderSourceInfo shaderInfo = descriptor.provider->GetDataInfo("spirv");
		m_EntryPointName = shaderInfo.entryPoint;
		uint32_t codeLength_integer = shaderInfo.dataLength / sizeof(uint32_t);
		std::vector<uint32_t> dataArray;
		dataArray.resize(codeLength_integer);
		memcpy(dataArray.data(), shaderInfo.dataPtr, shaderInfo.dataLength);
		vk::ShaderModuleCreateInfo shaderModuelCreateInfo(
			{}
			, dataArray
		);
		std::atomic_thread_fence(std::memory_order_release);
		m_ShaderModule = GetDevice().createShaderModule(shaderModuelCreateInfo);
	}
	void CShaderModuleObject::Create(ShaderSourceInfo const& shaderSourceInfo)
	{
		m_EntryPointName = shaderSourceInfo.entryPoint;
		uint32_t codeLength_integer = shaderSourceInfo.dataLength / sizeof(uint32_t);
		vk::ShaderModuleCreateInfo shaderModuelCreateInfo(
			{}
			, codeLength_integer
			, static_cast<uint32_t const*>(shaderSourceInfo.dataPtr)
		);
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

