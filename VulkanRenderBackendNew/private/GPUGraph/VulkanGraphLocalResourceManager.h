#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <ResourceManagement/VulkanResourceAliasing.h>
#include <ShaderResourceHandle.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAVector.h>

namespace graphics_backend
{
	// Graph-local temporary resource
	struct GraphLocalResource
	{
		enum class Type { Buffer, Texture };
		Type type;
		union
		{
			GPUBufferDescriptor bufferDesc;
			GPUTextureDescriptor textureDesc;
		};
		EBufferUsageFlags bufferUsage = EBufferUsageFlags{};
		ETextureAccessTypeFlags textureAccess = ETextureAccessTypeFlags{};
		uint32_t firstUseBatch = UINT32_MAX;
		uint32_t lastUseBatch = 0;
		uint64_t resourceId = 0;
	};

	// Managed GPU resource
	struct ManagedGPUResource
	{
		vk::Buffer buffer;
		vk::Image image;
		vk::ImageView imageView;
		VmaAllocation allocation;
		void* mappedPtr;
		uint64_t aliasedOffset = 0;
		GraphLocalResource const* localResource;
	};

	class VulkanGraphLocalResourceManager : public VulkanSubobjectBase
	{
	public:
		VulkanGraphLocalResourceManager() = default;
		~VulkanGraphLocalResourceManager() = default;

		void Init();
		virtual void Release() override;

		// Register a temporary buffer
		uint64_t RegisterTemporaryBuffer(GPUBufferDescriptor const& desc, EBufferUsageFlags usage, uint32_t batchIndex);

		// Add a buffer (wrapper around RegisterTemporaryBuffer)
		uint64_t AddBuffer(GPUBufferDescriptor const& desc, EBufferUsageFlags usage, uint32_t batchIndex);

		// Register a temporary texture
		uint64_t RegisterTemporaryTexture(GPUTextureDescriptor const& desc, ETextureAccessTypeFlags access, uint32_t batchIndex);

		// Mark resource usage for a batch
		void MarkResourceUse(uint64_t resourceId, uint32_t batchIndex);

		// Allocate all registered resources with aliasing
		bool AllocateAliasedResources();

		// Get managed resource
		ManagedGPUResource const* GetResource(uint64_t resourceId) const;

		// Get buffer resource by ID
		vk::Buffer GetBuffer(uint64_t resourceId) const;

		// Get texture resource by ID
		vk::Image GetTexture(uint64_t resourceId) const;
		vk::ImageView GetTextureView(uint64_t resourceId) const;

		// Register and get buffer by handle (for external/internal buffers)
		void RegisterBufferHandle(BufferHandle const& handle, uint64_t resourceId);
		void RegisterTextureHandle(ImageHandle const& handle, uint64_t resourceId);

		// Get buffer/texture by handle (looks up registered handles, falls back to external)
		vk::Buffer GetBuffer(BufferHandle const& handle) const;
		vk::Image GetTexture(ImageHandle const& handle) const;
		vk::ImageView GetTextureView(ImageHandle const& handle) const;

		// Release all resources
		void ReleaseAllResources();

		// Get statistics
		size_t GetResourceCount() const { return m_Resources.size(); }
		uint64_t GetTotalMemoryUsed() const { return m_TotalMemoryUsed; }

	private:
		// Phase A: create temp resources to query real memory requirements
		bool PhaseA_CreateTempResourcesAndGetReqs(castl::unordered_map<uint64_t, VkMemoryRequirements>& outMemReqs);

		// Phase B: bind a single buffer to aliased pool (shared with AddBuffer)
		bool BindBufferToAliasedPool(uint64_t id, VkDeviceMemory deviceMemory, void* poolMappedPtr);

		// Phase 2: bind all resources to physical memory at VirtualBlock offsets
		bool BindResourcesToPhysicalMemory();

		castl::unordered_map<uint64_t, GraphLocalResource> m_LocalResources;
		castl::unordered_map<uint64_t, ManagedGPUResource> m_Resources;
		VulkanResourceAliasing m_AliasingManager;

		// Handle to resource ID mappings
		castl::unordered_map<BufferHandle, uint64_t> m_BufferHandleToResource;
		castl::unordered_map<ImageHandle, uint64_t> m_TextureHandleToResource;

		uint64_t m_NextResourceId = 1;
		uint64_t m_TotalMemoryUsed = 0;
	};
}
