#include <GPUGraph/VulkanGraphLocalResourceManager.h>
#include <RenderBackend_Vulkan.h>
#include <VulkanObjects/VulkanBuffer.h>
#include <VulkanObjects/VulkanTexture.h>
#include <VulkanObjects/VulkanWindowHandle.h>

namespace
{
	using namespace graphics_backend;

	static vk::ImageAspectFlags GetImageAspectMask(ETextureFormat format)
	{
		if (FormatHasStencil(format))
			return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
		if (FormatHasDepth(format))
			return vk::ImageAspectFlagBits::eDepth;
		return vk::ImageAspectFlagBits::eColor;
	}

	static vk::ImageUsageFlags GetTextureImageUsage(ETextureAccessTypeFlags access)
	{
		vk::ImageUsageFlags usage{};
		if (access & ETextureAccessType::eSampled)
			usage |= vk::ImageUsageFlagBits::eSampled;
		if (access & ETextureAccessType::eRT)
			usage |= vk::ImageUsageFlagBits::eColorAttachment;
		if (access & ETextureAccessType::eDepthStencil)
			usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
		if (access & ETextureAccessType::eTransferSrc)
			usage |= vk::ImageUsageFlagBits::eTransferSrc;
		if (access & ETextureAccessType::eTransferDst)
			usage |= vk::ImageUsageFlagBits::eTransferDst;
		if (access & ETextureAccessType::eUnorderedAccess)
			usage |= vk::ImageUsageFlagBits::eStorage;
		if (usage == vk::ImageUsageFlags{})
			usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment;
		return usage;
	}

	static vk::BufferUsageFlags GetBufferUsageFlags(EBufferUsageFlags usage)
	{
		vk::BufferUsageFlags flags = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;
		if (usage & EBufferUsage::eVertexBuffer)
			flags |= vk::BufferUsageFlagBits::eVertexBuffer;
		if (usage & EBufferUsage::eIndexBuffer)
			flags |= vk::BufferUsageFlagBits::eIndexBuffer;
		if (usage & EBufferUsage::eStructuredBuffer)
			flags |= vk::BufferUsageFlagBits::eStorageBuffer;
		if (usage & EBufferUsage::eConstantBuffer)
			flags |= vk::BufferUsageFlagBits::eUniformBuffer;
		if (usage & EBufferUsage::eDataSrc)
			flags |= vk::BufferUsageFlagBits::eTransferSrc;
		if (usage & EBufferUsage::eDataDst)
			flags |= vk::BufferUsageFlagBits::eTransferDst;
		if (usage & EBufferUsage::eUnorderedAccess)
			flags |= vk::BufferUsageFlagBits::eStorageBuffer;
		return flags;
	}

	static uint64_t CalculateTextureSize(GPUTextureDescriptor const& desc)
	{
		uint32_t blockSize = GetFormatBlockSize(desc.format);
		uint64_t totalSize = 0;
		uint32_t w = desc.width;
		uint32_t h = desc.height;
		for (uint32_t mip = 0; mip < desc.mipLevels; ++mip)
		{
			uint32_t mipW = castl::max(1u, w >> mip);
			uint32_t mipH = castl::max(1u, h >> mip);
			if (IsCompressedFormat(desc.format))
			{
				mipW = (mipW + 3) / 4;
				mipH = (mipH + 3) / 4;
			}
			totalSize += mipW * mipH * blockSize;
		}
		totalSize *= desc.layers;
		return totalSize;
	}

	// ---- Shared CreateInfo fillers (used by Phase A, Phase B, and AddBuffer) ----

	static void FillBufferCreateInfo(GraphLocalResource const& resource, vk::BufferCreateInfo& bufferInfo)
	{
		bufferInfo.size = resource.bufferDesc.SizeInByte();
		bufferInfo.usage = GetBufferUsageFlags(resource.bufferUsage);
	}

	static void FillImageCreateInfo(GraphLocalResource const& resource, vk::ImageCreateInfo& imageInfo)
	{
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.format = VulkanTexture::ConvertFormat(resource.textureDesc.format);
		imageInfo.extent.width = resource.textureDesc.width;
		imageInfo.extent.height = resource.textureDesc.height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = resource.textureDesc.mipLevels;
		imageInfo.arrayLayers = resource.textureDesc.layers;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.usage = GetTextureImageUsage(resource.textureAccess);
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
	}
}

namespace graphics_backend
{
	void VulkanGraphLocalResourceManager::Init()
	{
		m_LocalResources.clear();
		m_Resources.clear();
		m_AliasingManager.Init();
		m_NextResourceId = 1;
		m_TotalMemoryUsed = 0;
		CA_LOG_INFO("VulkanGraphLocalResourceManager initialized");
	}

	void VulkanGraphLocalResourceManager::Release()
	{
		ReleaseAllResources();
		m_AliasingManager.Release();
		CA_LOG_INFO("VulkanGraphLocalResourceManager released");
	}

	uint64_t VulkanGraphLocalResourceManager::RegisterTemporaryBuffer(GPUBufferDescriptor const& desc, EBufferUsageFlags usage, uint32_t batchIndex)
	{
		uint64_t id = m_NextResourceId++;
		GraphLocalResource& resource = m_LocalResources[id];
		resource.type = GraphLocalResource::Type::Buffer;
		resource.bufferDesc = desc;
		resource.bufferUsage = usage;
		resource.firstUseBatch = batchIndex;
		resource.lastUseBatch = batchIndex;
		resource.resourceId = id;

		m_AliasingManager.RegisterResource(id, {
			batchIndex, batchIndex, desc.SizeInByte(), 256, true
		});

		return id;
	}

	uint64_t VulkanGraphLocalResourceManager::AddBuffer(GPUBufferDescriptor const& desc, EBufferUsageFlags usage, uint32_t batchIndex)
	{
		uint64_t id = RegisterTemporaryBuffer(desc, usage, batchIndex);

		// If aliased pool is already allocated, bind immediately
		if (m_AliasingManager.IsPoolAllocated())
		{
			if (!BindBufferToAliasedPool(id, m_AliasingManager.GetPoolDeviceMemory(), m_AliasingManager.GetMappedPtr()))
			{
				CA_LOG_WARN("VulkanGraphLocalResourceManager: AddBuffer failed to bind to aliased pool, resource {} may be unbound", id);
			}
		}
		// Otherwise: AllocateAliasedResources() will handle binding in Phase B

		return id;
	}

	uint64_t VulkanGraphLocalResourceManager::RegisterTemporaryTexture(GPUTextureDescriptor const& desc, ETextureAccessTypeFlags access, uint32_t batchIndex)
	{
		uint64_t id = m_NextResourceId++;
		GraphLocalResource& resource = m_LocalResources[id];
		resource.type = GraphLocalResource::Type::Texture;
		resource.textureDesc = desc;
		resource.textureAccess = access;
		resource.firstUseBatch = batchIndex;
		resource.lastUseBatch = batchIndex;
		resource.resourceId = id;

		// Calculate texture size based on format and mip chain
		uint64_t texSize = CalculateTextureSize(desc);

		m_AliasingManager.RegisterResource(id, {
			batchIndex, batchIndex, texSize, 256, false
		});

		return id;
	}

	void VulkanGraphLocalResourceManager::MarkResourceUse(uint64_t resourceId, uint32_t batchIndex)
	{
		auto it = m_LocalResources.find(resourceId);
		if (it != m_LocalResources.end())
		{
			it->second.firstUseBatch = castl::min(it->second.firstUseBatch, batchIndex);
			it->second.lastUseBatch = castl::max(it->second.lastUseBatch, batchIndex);
			m_AliasingManager.ExtendResourceLifetime(resourceId, it->second.firstUseBatch, it->second.lastUseBatch);
		}
	}

	bool VulkanGraphLocalResourceManager::PhaseA_CreateTempResourcesAndGetReqs(castl::unordered_map<uint64_t, VkMemoryRequirements>& outMemReqs)
	{
		auto device = GetDevice();

		for (auto const& [id, localResource] : m_LocalResources)
		{
			VkMemoryRequirements memReqs{};

			if (localResource.type == GraphLocalResource::Type::Buffer)
			{
				vk::BufferCreateInfo bufferInfo{};
				FillBufferCreateInfo(localResource, bufferInfo);

				vk::Buffer tempBuffer;
				try
				{
					tempBuffer = device.createBuffer(bufferInfo, nullptr);
				}
				catch (vk::SystemError const& e)
				{
					CA_LOG_ERR("VulkanGraphLocalResourceManager: Phase A failed to create temp buffer: {}", e.what());
					return false;
				}

				vk::MemoryRequirements vkMemReqs = device.getBufferMemoryRequirements(tempBuffer);
				memReqs.alignment = vkMemReqs.alignment;
				memReqs.size = vkMemReqs.size;
				memReqs.memoryTypeBits = vkMemReqs.memoryTypeBits;

				device.destroyBuffer(tempBuffer);
			}
			else
			{
				vk::ImageCreateInfo imageInfo{};
				FillImageCreateInfo(localResource, imageInfo);

				vk::Image tempImage;
				try
				{
					tempImage = device.createImage(imageInfo, nullptr);
				}
				catch (vk::SystemError const& e)
				{
					CA_LOG_ERR("VulkanGraphLocalResourceManager: Phase A failed to create temp image: {}", e.what());
					return false;
				}

				vk::MemoryRequirements vkMemReqs = device.getImageMemoryRequirements(tempImage);
				memReqs.alignment = vkMemReqs.alignment;
				memReqs.size = vkMemReqs.size;
				memReqs.memoryTypeBits = vkMemReqs.memoryTypeBits;

				device.destroyImage(tempImage);
			}

			outMemReqs[id] = memReqs;
		}

		return true;
	}

	bool VulkanGraphLocalResourceManager::BindBufferToAliasedPool(uint64_t id, VkDeviceMemory deviceMemory, void* poolMappedPtr)
	{
		auto it = m_LocalResources.find(id);
		if (it == m_LocalResources.end())
			return false;

		auto const& localResource = it->second;
		auto aliasedAlloc = m_AliasingManager.GetAliasedAllocation(id);
		auto device = GetDevice();

		ManagedGPUResource managed{};
		managed.localResource = &localResource;

		vk::BufferCreateInfo bufferInfo{};
		FillBufferCreateInfo(localResource, bufferInfo);

		vk::Buffer rawBuffer;
		try
		{
			rawBuffer = device.createBuffer(bufferInfo, nullptr);
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to create buffer for aliased binding: {}", e.what());
			return false;
		}

		// Get real alignment and compute aligned offset
		vk::MemoryRequirements vkMemReqs = device.getBufferMemoryRequirements(rawBuffer);

		// Fix F1: if resource was added after pool allocation (size==0 → not in plan),
		// append to end of pool rather than silently binding at offset 0.
		uint64_t alignedOffset;
		if (aliasedAlloc.size == 0)
		{
			// New resource — place after all planned resources
			uint64_t totalAliasedSize = m_AliasingManager.GetTotalAliasedSize();
			alignedOffset = (totalAliasedSize + vkMemReqs.alignment - 1) & ~(vkMemReqs.alignment - 1);
			// Update the aliasing plan so this resource's offset is tracked
			AliasedAllocation newAlloc{};
			newAlloc.offset = alignedOffset;
			newAlloc.size = vkMemReqs.size;
			newAlloc.allocation = m_AliasingManager.IsPoolAllocated() ? m_AliasingManager.GetAliasedAllocation(id).allocation : VK_NULL_HANDLE;
			newAlloc.mappedPtr = poolMappedPtr ? static_cast<uint8_t*>(poolMappedPtr) + alignedOffset : nullptr;
			// Update via the internal map — RegisterLateResource exposes the offset tracking
			m_AliasingManager.UpdateAliasedAllocationForLateResource(id, newAlloc, vkMemReqs.size);
		}
		else
		{
			alignedOffset = (aliasedAlloc.offset + vkMemReqs.alignment - 1) & ~(vkMemReqs.alignment - 1);
		}

		vk::DeviceMemory dm{ deviceMemory };
		try { device.bindBufferMemory(rawBuffer, dm, alignedOffset); }
		catch (vk::SystemError const& e) {
			CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to bind late buffer memory: {}", e.what());
			device.destroyBuffer(rawBuffer);
			return false;
		}

		managed.buffer = rawBuffer;
		managed.aliasedOffset = alignedOffset;
		managed.mappedPtr = poolMappedPtr
			? static_cast<uint8_t*>(poolMappedPtr) + alignedOffset
			: nullptr;

		m_Resources[id] = managed;
		m_TotalMemoryUsed += bufferInfo.size;
		return true;
	}

	bool VulkanGraphLocalResourceManager::AllocateAliasedResources()
	{
		// Propagate App pointer to aliasing manager child
		GetApp()->InitSubObj(&m_AliasingManager);

		// Step 1: Initial analysis with estimated sizes
		m_AliasingManager.AnalyzeAndPlanAliasing();

		// Phase A: Create temp resources to get real memory requirements
		castl::unordered_map<uint64_t, VkMemoryRequirements> realMemReqs;
		if (!PhaseA_CreateTempResourcesAndGetReqs(realMemReqs))
		{
			CA_LOG_ERR("VulkanGraphLocalResourceManager: Phase A failed");
			return false;
		}

		// Replan with real alignment and size from hardware
		m_AliasingManager.ReplanWithRealAlignment(realMemReqs);

		// === Try VirtualBlock two-phase path first (D3D12-equivalent aliasing) ===
		// Create VirtualBlocks on first frame; subsequent frames reuse via ResetVirtualBlocks
		if (!m_AliasingManager.GetBlockPool(0))
		{
			if (!m_AliasingManager.CreateVirtualBlocks())
				{
					m_AliasingManager.DestroyVirtualBlocks();
					goto FALLBACK_SINGLE_POOL;
				}
		}
		else
		{
			m_AliasingManager.ResetVirtualBlocks();
		}
		{
			// Phase 1: Per-batch virtual allocation — aliasing core
			uint32_t maxBatch = 0;
			for (auto const& [id, resource] : m_LocalResources)
			{
				if (resource.lastUseBatch > maxBatch) maxBatch = resource.lastUseBatch;
			}

			// Build per-batch registry from lifetime data
			castl::vector<castl::vector<uint64_t>> newOnBatch(maxBatch + 1);
			castl::vector<castl::vector<uint64_t>> dyingAfterBatch(maxBatch + 1);

			for (auto const& [id, resource] : m_LocalResources)
			{
				newOnBatch[resource.firstUseBatch].push_back(id);
				dyingAfterBatch[resource.lastUseBatch].push_back(id);
			}

			for (uint32_t batch = 0; batch <= maxBatch; ++batch)
			{
				// Free resources whose lifetime ends after this batch
				m_AliasingManager.FreeResourcesUpToBatch(batch);

				// Allocate new resources for this batch
				for (uint64_t resourceId : newOnBatch[batch])
				{
					auto rit = realMemReqs.find(resourceId);
					if (rit == realMemReqs.end()) continue;

					auto lit = m_LocalResources.find(resourceId);
					if (lit == m_LocalResources.end()) continue;

					bool isBuffer = (lit->second.type == GraphLocalResource::Type::Buffer);
					uint64_t offset;
					uint32_t blockIdx;
					if (!m_AliasingManager.AllocResource(resourceId, rit->second.size, rit->second.alignment,
						lit->second.firstUseBatch, lit->second.lastUseBatch, isBuffer,
						offset, blockIdx))
					{
						CA_LOG_WARN("VulkanGraphLocalResourceManager: Virtual alloc failed for resource {}", resourceId);
					}
				}
			}

			// Phase 2: Commit physical memory
			if (!m_AliasingManager.CommitVirtualAllocations())
			{
				CA_LOG_ERR("VulkanGraphLocalResourceManager: VirtualBlock commit failed, falling back to single-pool path");
				m_AliasingManager.DestroyVirtualBlocks();
				goto FALLBACK_SINGLE_POOL;
			}

			// Phase B: Bind resources to committed physical memory using virtual offsets
			if (!BindResourcesToPhysicalMemory())
			{
				CA_LOG_ERR("VulkanGraphLocalResourceManager: Physical binding failed");
				return false;
			}

			CA_LOG_INFO("VulkanGraphLocalResourceManager: Allocated {} resources via VirtualBlock aliasing"
				, m_Resources.size());
			return true;
		}

	FALLBACK_SINGLE_POOL:

		// === Existing single-pool path (fallback / incremental adoption) ===

		// Step 2: Allocate pool with corrected total size
		if (!m_AliasingManager.AllocateAliasedPool(m_AliasingManager.GetTotalAliasedSize()))
		{
			CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to allocate aliased pool");
			return false;
		}

		// Fixup allocations after pool allocation
		m_AliasingManager.UpdateAliasedAllocationsMap();

		// Get shared pool resources
		VkDeviceMemory deviceMemory = m_AliasingManager.GetPoolDeviceMemory();
		void* poolMappedPtr = m_AliasingManager.GetMappedPtr();
		auto device = GetDevice();

		// Phase B: Create real resources bound to aliased pool
		vk::DeviceMemory dm{ deviceMemory };
		uint32_t poolMemTypeBit = 1u << m_AliasingManager.GetPoolMemoryType();

		for (auto const& [id, localResource] : m_LocalResources)
		{
			ManagedGPUResource managed{};
			managed.localResource = &localResource;

			auto aliasedAlloc = m_AliasingManager.GetAliasedAllocation(id);

			if (localResource.type == GraphLocalResource::Type::Buffer)
			{
				vk::BufferCreateInfo bufferInfo{};
				FillBufferCreateInfo(localResource, bufferInfo);

				vk::Buffer rawBuffer;
				try { rawBuffer = device.createBuffer(bufferInfo, nullptr); }
				catch (vk::SystemError const& e) {
					CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to create buffer: {}", e.what());
					return false;
				}

				vk::MemoryRequirements vkMemReqs = device.getBufferMemoryRequirements(rawBuffer);
				uint64_t alignedOffset = (aliasedAlloc.offset + vkMemReqs.alignment - 1) & ~(vkMemReqs.alignment - 1);

				// --- Route A: Aliased pool binding (if memory type compatible) ---
				if ((vkMemReqs.memoryTypeBits & poolMemTypeBit) != 0)
				{
					try { device.bindBufferMemory(rawBuffer, dm, alignedOffset); }
					catch (vk::SystemError const& e) {
						CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to bind buffer memory: {}", e.what());
						device.destroyBuffer(rawBuffer);
						return false;
					}
					managed.buffer = rawBuffer;
					managed.aliasedOffset = alignedOffset;
					managed.mappedPtr = poolMappedPtr
						? static_cast<uint8_t*>(poolMappedPtr) + alignedOffset : nullptr;
				}
				else
				{
					// --- Route B: VMA standalone allocation (memory type incompatible with pool) ---
					CA_LOG_INFO("VulkanGraphLocalResourceManager: Buffer allocated standalone (memory type incompatibility)");
					device.destroyBuffer(rawBuffer);

					auto& memManager = GetApp()->GetMemoryManager();
					VmaAllocationCreateInfo vmaAllocInfo{};
					vmaAllocInfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
					vmaAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
					vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

					VmaAllocationInfo allocInfoOut{};
					managed.allocation = memManager.AllocateBuffer(bufferInfo, vmaAllocInfo, managed.buffer, &allocInfoOut);
					managed.mappedPtr = allocInfoOut.pMappedData;
				}
			}
			else
			{
				vk::ImageCreateInfo imageInfo{};
				FillImageCreateInfo(localResource, imageInfo);

				vk::Image rawImage;
				try { rawImage = device.createImage(imageInfo, nullptr); }
				catch (vk::SystemError const& e) {
					CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to create image: {}", e.what());
					return false;
				}

				vk::MemoryRequirements vkMemReqs = device.getImageMemoryRequirements(rawImage);
				uint64_t alignedOffset = (aliasedAlloc.offset + vkMemReqs.alignment - 1) & ~(vkMemReqs.alignment - 1);

				// --- Route A: Aliased pool binding ---
				if ((vkMemReqs.memoryTypeBits & poolMemTypeBit) != 0)
				{
					try { device.bindImageMemory(rawImage, dm, alignedOffset); }
					catch (vk::SystemError const& e) {
						CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to bind image memory: {}", e.what());
						device.destroyImage(rawImage);
						return false;
					}
					managed.image = rawImage;
					managed.aliasedOffset = alignedOffset;
					managed.mappedPtr = poolMappedPtr
						? static_cast<uint8_t*>(poolMappedPtr) + alignedOffset : nullptr;
				}
				else
				{
					// --- Route B: VMA standalone allocation ---
					CA_LOG_INFO("VulkanGraphLocalResourceManager: Image allocated standalone (memory type incompatibility)");
					device.destroyImage(rawImage);

					auto& memManager = GetApp()->GetMemoryManager();
					VmaAllocationCreateInfo vmaAllocInfo{};
					vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

					VmaAllocationInfo allocInfoOut{};
					managed.allocation = memManager.AllocateImage(imageInfo, vmaAllocInfo, managed.image, &allocInfoOut);
					managed.mappedPtr = nullptr;
				}

				// Create image view (common to both routes)
				vk::ImageViewCreateInfo viewInfo{};
				viewInfo.image = managed.image;
				viewInfo.viewType = vk::ImageViewType::e2D;
				viewInfo.format = imageInfo.format;
				viewInfo.subresourceRange.aspectMask = GetImageAspectMask(localResource.textureDesc.format);
				viewInfo.subresourceRange.levelCount = imageInfo.mipLevels;
				viewInfo.subresourceRange.layerCount = imageInfo.arrayLayers;

				try { managed.imageView = device.createImageView(viewInfo); }
				catch (vk::SystemError const& e) {
					CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to create image view: {}", e.what());
					if (managed.allocation)
						GetApp()->GetMemoryManager().FreeImage(managed.image, managed.allocation);
					else
						device.destroyImage(managed.image);
					return false;
				}
			}

			m_Resources[id] = managed;
		}

		m_TotalMemoryUsed = m_AliasingManager.GetTotalAliasedSize();

		CA_LOG_INFO("VulkanGraphLocalResourceManager: Allocated {} resources, total memory: {}"
			, m_Resources.size(), m_TotalMemoryUsed);
		return true;
	}

	bool VulkanGraphLocalResourceManager::BindResourcesToPhysicalMemory()
	{
		auto device = GetDevice();
		auto const& activeAllocs = m_AliasingManager.GetActiveVirtualAllocs();

		for (auto const& [id, localResource] : m_LocalResources)
		{
			auto allocIt = activeAllocs.find(id);
			if (allocIt == activeAllocs.end())
				continue;

			auto const& virtAlloc = allocIt->second;
			auto const* blockPool = m_AliasingManager.GetBlockPool(virtAlloc.blockIndex);
			if (!blockPool || !blockPool->deviceMemory)
				continue;

			ManagedGPUResource managed{};
			managed.localResource = &localResource;
			managed.aliasedOffset = virtAlloc.offset;
			managed.mappedPtr = blockPool->mappedPtr
				? static_cast<uint8_t*>(blockPool->mappedPtr) + virtAlloc.offset : nullptr;

			if (localResource.type == GraphLocalResource::Type::Buffer)
			{
				vk::BufferCreateInfo bufferInfo{};
				FillBufferCreateInfo(localResource, bufferInfo);

				try { managed.buffer = device.createBuffer(bufferInfo, nullptr); }
				catch (vk::SystemError const& e) {
					CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to create buffer in Phase 2: {}", e.what());
					return false;
				}

				vk::DeviceMemory dm{ blockPool->deviceMemory };
				try { device.bindBufferMemory(managed.buffer, dm, virtAlloc.offset); }
				catch (vk::SystemError const& e) {
					CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to bind buffer to virtual offset {}: {}",
						virtAlloc.offset, e.what());
					device.destroyBuffer(managed.buffer);
					return false;
				}
			}
			else
			{
				vk::ImageCreateInfo imageInfo{};
				FillImageCreateInfo(localResource, imageInfo);

				try { managed.image = device.createImage(imageInfo, nullptr); }
				catch (vk::SystemError const& e) {
					CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to create image in Phase 2: {}", e.what());
					return false;
				}

				vk::DeviceMemory dm{ blockPool->deviceMemory };
				try { device.bindImageMemory(managed.image, dm, virtAlloc.offset); }
				catch (vk::SystemError const& e) {
					CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to bind image to virtual offset {}: {}",
						virtAlloc.offset, e.what());
					device.destroyImage(managed.image);
					return false;
				}

				// Create image view
				vk::ImageViewCreateInfo viewInfo{};
				viewInfo.image = managed.image;
				viewInfo.viewType = vk::ImageViewType::e2D;
				viewInfo.format = imageInfo.format;
				viewInfo.subresourceRange.aspectMask = GetImageAspectMask(localResource.textureDesc.format);
				viewInfo.subresourceRange.levelCount = imageInfo.mipLevels;
				viewInfo.subresourceRange.layerCount = imageInfo.arrayLayers;

				try { managed.imageView = device.createImageView(viewInfo); }
				catch (vk::SystemError const& e) {
					CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to create image view in Phase 2: {}", e.what());
					device.destroyImage(managed.image);
					return false;
				}
			}

			m_Resources[id] = managed;
		}

		m_TotalMemoryUsed = m_AliasingManager.GetTotalAliasedSize();
		return true;
	}

	ManagedGPUResource const* VulkanGraphLocalResourceManager::GetResource(uint64_t resourceId) const
	{
		auto it = m_Resources.find(resourceId);
		if (it != m_Resources.end())
		{
			return &it->second;
		}
		return nullptr;
	}

	vk::Buffer VulkanGraphLocalResourceManager::GetBuffer(uint64_t resourceId) const
	{
		auto* resource = GetResource(resourceId);
		return resource ? resource->buffer : nullptr;
	}

	vk::Image VulkanGraphLocalResourceManager::GetTexture(uint64_t resourceId) const
	{
		auto* resource = GetResource(resourceId);
		return resource ? resource->image : nullptr;
	}

	vk::ImageView VulkanGraphLocalResourceManager::GetTextureView(uint64_t resourceId) const
	{
		auto* resource = GetResource(resourceId);
		return resource ? resource->imageView : nullptr;
	}

	void VulkanGraphLocalResourceManager::ReleaseAllResources()
	{
		auto device = GetDevice();
		auto& memManager = GetApp()->GetMemoryManager();

		for (auto& [id, resource] : m_Resources)
		{
			if (resource.buffer)
			{
				if (resource.allocation)
				{
					// Standalone VMA allocation (fallback path)
					memManager.FreeBuffer(resource.buffer, resource.allocation);
				}
				else
				{
					// Aliased pool binding — just destroy the handle
					device.destroyBuffer(resource.buffer);
				}
			}
			if (resource.imageView)
			{
				device.destroyImageView(resource.imageView);
			}
			if (resource.image)
			{
				if (resource.allocation)
				{
					memManager.FreeImage(resource.image, resource.allocation);
				}
				else
				{
					device.destroyImage(resource.image);
				}
			}
		}

		m_Resources.clear();
		m_LocalResources.clear();
		m_BufferHandleToResource.clear();
		m_TextureHandleToResource.clear();
		m_AliasingManager.FreeAliasedPool();
		m_TotalMemoryUsed = 0;

		CA_LOG_INFO("VulkanGraphLocalResourceManager: Released all resources");
	}

	void VulkanGraphLocalResourceManager::RegisterBufferHandle(BufferHandle const& handle, uint64_t resourceId)
	{
		if (handle.IsValid())
		{
			m_BufferHandleToResource[handle] = resourceId;
		}
	}

	void VulkanGraphLocalResourceManager::RegisterTextureHandle(ImageHandle const& handle, uint64_t resourceId)
	{
		if (handle.IsValid())
		{
			m_TextureHandleToResource[handle] = resourceId;
		}
	}

	vk::Buffer VulkanGraphLocalResourceManager::GetBuffer(BufferHandle const& handle) const
	{
		if (!handle.IsValid())
			return nullptr;

		// First check if we have a registered local resource
		auto it = m_BufferHandleToResource.find(handle);
		if (it != m_BufferHandleToResource.end())
		{
			return GetBuffer(it->second);
		}

		// Fall back to external buffer
		switch (handle.GetType())
		{
		case BufferHandle::BufferType::External:
		{
			VulkanBuffer const* pBuffer = handle.GetBufferPtr<VulkanBuffer>();
			if (pBuffer)
			{
				return pBuffer->GetBuffer();
			}
			break;
		}
		case BufferHandle::BufferType::Internal:
			// Internal buffers should be registered
			CA_LOG_WARN("VulkanGraphLocalResourceManager: Internal buffer not registered");
			break;
		}

		return nullptr;
	}

	vk::Image VulkanGraphLocalResourceManager::GetTexture(ImageHandle const& handle) const
	{
		if (!handle.IsValid())
			return nullptr;

		// First check if we have a registered local resource
		auto it = m_TextureHandleToResource.find(handle);
		if (it != m_TextureHandleToResource.end())
		{
			return GetTexture(it->second);
		}

		// Fall back to external texture or backbuffer
		switch (handle.GetType())
		{
		case ImageHandle::ImageType::External:
		{
			VulkanTexture const* pTexture = handle.GetTexturePtr<VulkanTexture>();
			if (pTexture)
			{
				return pTexture->GetImage();
			}
			break;
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			VulkanWindowHandle const* pWindow = handle.GetWindowPtr<VulkanWindowHandle>();
			if (pWindow)
			{
				return pWindow->GetCurrentImage();
			}
			break;
		}
		case ImageHandle::ImageType::Internal:
			// Internal textures should be registered
			CA_LOG_WARN("VulkanGraphLocalResourceManager: Internal texture not registered");
			break;
		}

		return nullptr;
	}

	vk::ImageView VulkanGraphLocalResourceManager::GetTextureView(ImageHandle const& handle) const
	{
		if (!handle.IsValid())
			return nullptr;

		// First check if we have a registered local resource
		auto it = m_TextureHandleToResource.find(handle);
		if (it != m_TextureHandleToResource.end())
		{
			return GetTextureView(it->second);
		}

		// Fall back to external texture or backbuffer
		switch (handle.GetType())
		{
		case ImageHandle::ImageType::External:
		{
			VulkanTexture const* pTexture = handle.GetTexturePtr<VulkanTexture>();
			if (pTexture)
			{
				return pTexture->GetImageView();
			}
			break;
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			VulkanWindowHandle const* pWindow = handle.GetWindowPtr<VulkanWindowHandle>();
			if (pWindow)
			{
				return pWindow->GetCurrentImageView();
			}
			break;
		}
		case ImageHandle::ImageType::Internal:
			// Internal textures should be registered
			CA_LOG_WARN("VulkanGraphLocalResourceManager: Internal texture view not registered");
			break;
		}

		return nullptr;
	}
}
