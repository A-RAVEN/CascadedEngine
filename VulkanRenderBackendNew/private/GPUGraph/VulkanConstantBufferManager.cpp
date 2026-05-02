#include <GPUGraph/VulkanConstantBufferManager.h>
#include <GPUGraph/VulkanGraphLocalResourceManager.h>
#include <VulkanObjects/VulkanShaderStruct.h>

namespace graphics_backend
{
	uint64_t VulkanConstantBufferManager::GetOrCreateResourceId(
		VulkanShaderStruct const* pShaderStruct,
		VulkanGraphLocalResourceManager& resourceManager,
		GPUBufferDescriptor const& desc)
	{
		return m_CBufferResources.get_or_create(pShaderStruct, [&](VulkanShaderStruct const* inKey) -> uint64_t
		{
			return resourceManager.RegisterTemporaryBuffer(desc, EBufferUsage::eConstantBuffer, 0);
		})->second;
	}

	uint64_t VulkanConstantBufferManager::GetResourceId(VulkanShaderStruct const* pShaderStruct) const
	{
		auto const* pValue = m_CBufferResources.try_get(pShaderStruct);
		return pValue ? *pValue : 0;
	}

	void VulkanConstantBufferManager::Clear()
	{
		m_CBufferResources.clear();
	}

	void VulkanConstantBufferManager::Release()
	{
		Clear();
	}
}
