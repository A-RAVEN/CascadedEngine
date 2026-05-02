#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <CACore/CASharedDic.h>
#include <GPUBuffer.h>

namespace graphics_backend
{
	class VulkanShaderStruct;
	class VulkanGraphLocalResourceManager;

	class VulkanConstantBufferManager : public VulkanSubobjectBase
	{
	public:
		VulkanConstantBufferManager() = default;

		// Get or create a unique resource ID for a ShaderStruct's CBuffer.
		// Uses shared_dic::get_or_create to guarantee one resource per struct.
		// Internally calls RegisterTemporaryBuffer (not AddBuffer) so CBuffer
		// participates in aliasing allocation.
		uint64_t GetOrCreateResourceId(VulkanShaderStruct const* pShaderStruct,
			VulkanGraphLocalResourceManager& resourceManager,
			GPUBufferDescriptor const& desc);

		// Query existing resource ID. Returns 0 if not found.
		uint64_t GetResourceId(VulkanShaderStruct const* pShaderStruct) const;

		// Clear all mappings (per-frame reset)
		void Clear();

		// Release (GPU resources managed by LocalResourceManager, so same as Clear)
		void Release();

	private:
		castl::shared_dic<VulkanShaderStruct const*, uint64_t> m_CBufferResources;
	};
}
