#pragma once
#include "D3D12Includes.h"
#include <Utils/D3D12SubobjectBase.h>
#include <Common.h>
#include <CASTL/CAUnorderedMap.h>
#include <GPUGraph.h>
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <ResourceManagment/MemoryManager.h>
#include <DescriptorManagment/GPUDescriptorHeap.h>
#include <CASTL/CAUnorderedSet.h>

namespace graphics_backend
{
	enum class EResourceUsage : uint32_t
	{
		eNone = 0,
		eShaderResource = 1 << 0,
		eShaderUnorderedAccess = 1 << 1,
		eRenderTarget = 1 << 2,
		eVertexInput = 1 << 3,
		eIndexInput = 1 << 4,
		eCopy = 1 << 5,
		eConstantBuffer = 1 << 6,
		eDepthStencilTarget = 1 << 7,
		eInitialized = 1 << 8,
		ePresent = 1 << 9,
		eShaderInputs = eShaderResource | eShaderUnorderedAccess | eConstantBuffer,
		eComputeQueueMask = eShaderResource | eShaderUnorderedAccess | eCopy | eConstantBuffer | eInitialized,
		eAll = ~0,
		eBitMax = 10,
	};
	enum class EGPUQueueType : uint32_t
	{
		eNone = 0,
		eDirect = 1 << 0,
		eCompute = 1 << 1,
		eCopy = 1 << 2,
	};

	enum class EResourceViewType
	{
		eSRV,
		eUAV,
		eRTV,
		eDSV,
		eCBV,
	};
}
CA_ENUM_FLAGS_NAMESPACE(EResourceUsage, graphics_backend);
CA_ENUM_FLAGS_NAMESPACE(EGPUQueueType, graphics_backend);

namespace graphics_backend
{
	struct ResourceBarrierUsageStates
	{
		D3D12_BARRIER_ACCESS accessState;
		D3D12_BARRIER_LAYOUT layoutState;
		D3D12_BARRIER_LAYOUT queueLocalLayoutState;
		D3D12_BARRIER_SYNC barrierSync;
		EGPUQueueTypeFlags queueTypes;
		bool HasQueueLocalState()
		{
			return layoutState != queueLocalLayoutState;
		}
	};

	static void IterateResourceUsages(EResourceUsageFlags flags, castl::function<void(EResourceUsage)> callback)
	{
		for (uint32_t i = 0; i < static_cast<uint32_t>(EResourceUsage::eBitMax); ++i)
		{
			EResourceUsage usage = static_cast<EResourceUsage>(1 << i);
			if (flags & usage)
			{
				callback(usage);
			}
		}
	}

	struct ResourceState
	{
		ShaderCompilerSlang::EShaderResourceAccess resourceAccess;
		EShaderTypeFlags shaderStages;

		EResourceUsageFlags resourceUsage;
		EGPUQueueTypeFlags queueTypes;
		bool isImage;

		bool operator==(const ResourceState& other) const
		{
			return resourceAccess == other.resourceAccess
				&& shaderStages == other.shaderStages
				&& resourceUsage == other.resourceUsage
				&& queueTypes == other.queueTypes
				&& isImage == other.isImage;
		}

		//Check 
		bool isDirectQueueLocal() const
		{
			return queueTypes == EGPUQueueType::eDirect;
		}

		bool isComputeQueueLocal() const
		{
			return queueTypes == EGPUQueueType::eCompute;
		}

		bool hasComputeQueue() const
		{
			return (queueTypes & EGPUQueueType::eCompute) == EGPUQueueType::eCompute;
		}

		bool hasDirectQueue() const
		{
			return (queueTypes & EGPUQueueType::eDirect) == EGPUQueueType::eDirect;
		}

		//bool isDirectOrComputeQueueLocal() const
		//{
		//	return isDirectQueueLocal() || isComputeQueueLocal();
		//}

		bool isSharedBetweenQueues() const
		{
			return hasComputeQueue() && hasDirectQueue();
		}

		bool isUndefined() const
		{
			return resourceUsage == EResourceUsage::eInitialized;
		}

		bool isPresent() const
		{
			return resourceUsage == EResourceUsage::ePresent;
		}

		static ResourceState InitializedState()
		{
			ResourceState result;
			result.resourceAccess = ShaderCompilerSlang::EShaderResourceAccess::eUnknown;
			result.shaderStages = EShaderTypeMask::eNone;
			result.resourceUsage = EResourceUsage::eInitialized;
			result.queueTypes = EGPUQueueType::eNone;
			return result;
		}

		static ResourceState PresentState()
		{
			ResourceState result;
			result.resourceAccess = ShaderCompilerSlang::EShaderResourceAccess::eUnknown;
			result.shaderStages = EShaderTypeMask::eNone;
			result.resourceUsage = EResourceUsage::ePresent;
			result.queueTypes = EGPUQueueType::eDirect;
			return result;
		}

		bool NotUsed() const
		{
			if (resourceUsage == 0)
				return true;
			//Resource Is Used As Shader Argument But No Shader Stage Use It
			EResourceUsageFlags shaderResourceUsages = EResourceUsage::eShaderInputs;
			bool shaderInputesOnly = ((resourceUsage & ~(shaderResourceUsages)) == 0);
			return shaderInputesOnly && (shaderStages == 0);
		}

		bool ReadOnly() const
		{
			switch (resourceAccess)
			{
			case ShaderCompilerSlang::EShaderResourceAccess::eReadOnly:
				return true;
			default:
				return false;
			}
		}
		bool Read() const
		{
			switch (resourceAccess)
			{
			case ShaderCompilerSlang::EShaderResourceAccess::eReadOnly:
			case ShaderCompilerSlang::EShaderResourceAccess::eReadWrite:
				return true;
			default:
				return false;
			}
		}
		bool Write() const
		{
			switch (resourceAccess)
			{
			case ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly:
			case ShaderCompilerSlang::EShaderResourceAccess::eReadWrite:
				return true;
			default:
				return false;
			}
		}
		bool CompatibleToCombine(ResourceState const& other) const
		{
			//TODO: Do Better Combine Check
			CA_ASSERT_BREAK(isImage == other.isImage, "Incompatible IsImage State!");
			bool accessEqual = resourceAccess == other.resourceAccess;
			bool usageEqual = resourceUsage == other.resourceUsage;
			return accessEqual && usageEqual;
		}
		void Combine(ResourceState const& other)
		{
			CA_ASSERT_BREAK(isImage == other.isImage, "Incompatible IsImage State!");
			CA_ASSERT_BREAK(resourceAccess == other.resourceAccess, "Resource Access Not Compatible");
			shaderStages |= other.shaderStages;
			resourceUsage |= other.resourceUsage;
			queueTypes |= other.queueTypes;
		}
		bool AnyStateChange(ResourceState const& otherResourceState) const
		{
			if (resourceUsage != otherResourceState.resourceUsage)
				return true;
			if (Read() && otherResourceState.Write())
				return true;
			if (Write() && otherResourceState.Read())
				return true;
		}
	};

	using ResourceUsageRange = castl::range<uint32_t>;

	struct CBufferUsageData
	{
		ResourceUsageRange lifeTime;
		EGPUQueueTypeFlags queueTypes = EGPUQueueType::eNone;

		void Encapsule(uint32_t passID, EGPUQueueTypeFlags queueType)
		{
			queueTypes |= queueType;
			lifeTime.encapsule(passID);
		}
	};

	struct ResourceUsageRangeData
	{
		ResourceUsageRange lifeTime;
		EResourceUsageFlags allUsages = EResourceUsage::eNone;

		struct BatchAndState
		{
			uint32_t batchID;
			ResourceState state;
		};
		castl::vector<BatchAndState> states;

		void Expand(uint32_t passID, ResourceState const& resourceState)
		{
			allUsages |= resourceState.resourceUsage;
			states.push_back({ passID, resourceState });
			lifeTime.encapsule(passID);
		}
	};

	constexpr D3D12_BARRIER_SYNC DetermingShaderStageSync(EShaderTypeFlags flags)
	{
		D3D12_BARRIER_SYNC result = D3D12_BARRIER_SYNC_NONE;
		if (flags & EShaderTypeMask::eComp)
		{
			result |= D3D12_BARRIER_SYNC_COMPUTE_SHADING;
		}

		EShaderTypeFlags vertStages = EShaderTypeFlags{ EShaderTypeMask::eVert } | EShaderTypeMask::eTessCtr | EShaderTypeMask::eTessEvl | EShaderTypeMask::eGeom
			| EShaderTypeMask::eMesh | EShaderTypeMask::eTask;
		if (flags & vertStages)
		{
			result |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
		}

		if (flags & EShaderTypeMask::eFrag)
		{
			result |= D3D12_BARRIER_SYNC_PIXEL_SHADING;
		}

		if (flags & EShaderTypeMask::eAllRaytracing)
		{
			result |= D3D12_BARRIER_SYNC_RAYTRACING;
		}
		if (result == D3D12_BARRIER_SYNC_NONE)
		{
			CA_LOG_ERR("No Shader Stage Sync Detected, Using All Shading Sync");
			result = D3D12_BARRIER_SYNC_ALL_SHADING;
		}
		return result;
	}

	struct AccessFlagsToLayout
	{
		D3D12_BARRIER_LAYOUT layout;
		D3D12_BARRIER_ACCESS flags;
	};

	constexpr static AccessFlagsToLayout DirectQueueFlagsToLayouts[] = {
		{
			D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COMMON,
			D3D12_BARRIER_ACCESS_COPY_SOURCE
			| D3D12_BARRIER_ACCESS_COPY_DEST
			| D3D12_BARRIER_ACCESS_SHADER_RESOURCE
			| D3D12_BARRIER_ACCESS_UNORDERED_ACCESS
		},
		{
			D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ,
			D3D12_BARRIER_ACCESS_SHADER_RESOURCE
			| D3D12_BARRIER_ACCESS_COPY_SOURCE
			| D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ
			| D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE
			| D3D12_BARRIER_ACCESS_RESOLVE_SOURCE
		},
		{
			D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_UNORDERED_ACCESS,
			D3D12_BARRIER_ACCESS_UNORDERED_ACCESS
		},
		{
			D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE,
			D3D12_BARRIER_ACCESS_SHADER_RESOURCE
		},
		{
			D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_SOURCE,
			D3D12_BARRIER_ACCESS_COPY_SOURCE
		},
		{
			D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_DEST,
			D3D12_BARRIER_ACCESS_COPY_DEST
		},
	};

	constexpr static AccessFlagsToLayout ComputeQueueFlagsToLayouts[] = {

		{
			D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_UNORDERED_ACCESS,
			D3D12_BARRIER_ACCESS_UNORDERED_ACCESS
		},
		{
			D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_SHADER_RESOURCE,
			D3D12_BARRIER_ACCESS_SHADER_RESOURCE
		},
		{
			D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_SOURCE,
			D3D12_BARRIER_ACCESS_COPY_SOURCE
		},
		{
			D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_DEST,
			D3D12_BARRIER_ACCESS_COPY_DEST
		},
		{
			D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COMMON,
			D3D12_BARRIER_ACCESS_COPY_SOURCE
			| D3D12_BARRIER_ACCESS_COPY_DEST
			| D3D12_BARRIER_ACCESS_SHADER_RESOURCE
			| D3D12_BARRIER_ACCESS_UNORDERED_ACCESS
		},
		{
			D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_GENERIC_READ,
			D3D12_BARRIER_ACCESS_SHADER_RESOURCE
			| D3D12_BARRIER_ACCESS_COPY_SOURCE
		},

	};

	constexpr static AccessFlagsToLayout CommonAccessFlagsToLayouts[] = {
		{
			D3D12_BARRIER_LAYOUT_UNDEFINED,
			D3D12_BARRIER_ACCESS_NO_ACCESS,
		},
		{
			D3D12_BARRIER_LAYOUT_RENDER_TARGET,
			D3D12_BARRIER_ACCESS_RENDER_TARGET
		},
		{
			D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,
			D3D12_BARRIER_ACCESS_UNORDERED_ACCESS
		},
		{
			D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ,
			D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ
		},

		{
			D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
			D3D12_BARRIER_ACCESS_SHADER_RESOURCE
		},
		{
			D3D12_BARRIER_LAYOUT_COPY_SOURCE,
			D3D12_BARRIER_ACCESS_COPY_SOURCE
		},
		{
			D3D12_BARRIER_LAYOUT_COPY_DEST,
			D3D12_BARRIER_ACCESS_COPY_DEST
		},
		{
			D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE,
			D3D12_BARRIER_ACCESS_RESOLVE_SOURCE
		},
		{
			D3D12_BARRIER_LAYOUT_RESOLVE_DEST,
			D3D12_BARRIER_ACCESS_RESOLVE_DEST
		},
		{
			D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE,
			D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE
		},
		{
			D3D12_BARRIER_LAYOUT_VIDEO_DECODE_READ,
			D3D12_BARRIER_ACCESS_VIDEO_DECODE_READ
		},
		{
			D3D12_BARRIER_LAYOUT_VIDEO_DECODE_WRITE,
			D3D12_BARRIER_ACCESS_VIDEO_DECODE_WRITE
		},
		{
			D3D12_BARRIER_LAYOUT_VIDEO_PROCESS_READ,
			D3D12_BARRIER_ACCESS_VIDEO_PROCESS_READ
		},
		{
			D3D12_BARRIER_LAYOUT_VIDEO_PROCESS_WRITE,
			D3D12_BARRIER_ACCESS_VIDEO_PROCESS_WRITE
		},
		{
			D3D12_BARRIER_LAYOUT_VIDEO_ENCODE_READ,
			D3D12_BARRIER_ACCESS_VIDEO_ENCODE_READ
		},
		{
			D3D12_BARRIER_LAYOUT_VIDEO_ENCODE_WRITE,
			D3D12_BARRIER_ACCESS_VIDEO_ENCODE_WRITE
		},
		{
			D3D12_BARRIER_LAYOUT_COMMON,
			D3D12_BARRIER_ACCESS_SHADER_RESOURCE
			| D3D12_BARRIER_ACCESS_COPY_DEST
			| D3D12_BARRIER_ACCESS_COPY_SOURCE
		},
		{
			D3D12_BARRIER_LAYOUT_GENERIC_READ,
			D3D12_BARRIER_ACCESS_SHADER_RESOURCE
			| D3D12_BARRIER_ACCESS_COPY_SOURCE
		},
		{
			D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
			D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ
			| D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE
		},
		{
			D3D12_BARRIER_LAYOUT_VIDEO_QUEUE_COMMON,
			D3D12_BARRIER_ACCESS_COPY_SOURCE
			| D3D12_BARRIER_ACCESS_COPY_DEST
		},
	};

	static void DetermineLayoutByAccessFlags(bool isImage
		, EGPUQueueTypeFlags queueType
		, D3D12_BARRIER_ACCESS accessFlags
		, D3D12_BARRIER_LAYOUT& outDefaultLayout
		, D3D12_BARRIER_LAYOUT& outQueueLocalLayout
	)
	{
		if (!isImage)
		{
			outDefaultLayout = outQueueLocalLayout = D3D12_BARRIER_LAYOUT_COMMON;
			return;
		}
		outDefaultLayout = outQueueLocalLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;
		for (auto& flagsToLayout : CommonAccessFlagsToLayouts)
		{
			if ((accessFlags & flagsToLayout.flags) == accessFlags)
			{
				outDefaultLayout = outQueueLocalLayout = flagsToLayout.layout;
				break;
			}
		}
		if (queueType == EGPUQueueType::eDirect)
		{
			for (auto& flagsToLayout : DirectQueueFlagsToLayouts)
			{
				if ((accessFlags & flagsToLayout.flags) == accessFlags)
				{
					outQueueLocalLayout = flagsToLayout.layout;
					break;
				}
			}
		}
		else if (queueType == EGPUQueueType::eCompute)
		{
			for (auto& flagsToLayout : ComputeQueueFlagsToLayouts)
			{
				if ((accessFlags & flagsToLayout.flags) == accessFlags)
				{
					outQueueLocalLayout = flagsToLayout.layout;
					break;
				}
			}
		}

		CA_ASSERT_BREAK(outDefaultLayout != D3D12_BARRIER_LAYOUT_UNDEFINED, "Cannot Determine Layout For Access Flags: {}", accessFlags);
		CA_ASSERT_BREAK(outQueueLocalLayout != D3D12_BARRIER_LAYOUT_UNDEFINED, "Cannot Determine Queue Local Layout For Access Flags: {}", accessFlags);
	}

	static bool ResourceUsageCompatible(EResourceUsageFlags resourceUsages, EGPUQueueTypeFlags queueTypes)
	{
		if (queueTypes.HasAny(EGPUQueueType::eDirect))
		{
			EResourceUsageFlags directQueueUsages = EResourceUsage::eAll;
			if (!directQueueUsages.HasAll(resourceUsages))
				return false;
		}
		if (queueTypes.HasAny(EGPUQueueType::eCompute))
		{
			EResourceUsageFlags computeQueueUsages = EResourceUsage::eComputeQueueMask;
			if (!computeQueueUsages.HasAll(resourceUsages))
				return false;
		}
		return true;
	}

	static ResourceBarrierUsageStates DetermineResourceBarrierUsageStates(ResourceState const& resourceState)
	{
		if (resourceState.isUndefined())
		{
			ResourceBarrierUsageStates undefinedResult;
			undefinedResult.accessState = D3D12_BARRIER_ACCESS_NO_ACCESS;
			undefinedResult.layoutState = D3D12_BARRIER_LAYOUT_UNDEFINED;
			undefinedResult.queueLocalLayoutState = D3D12_BARRIER_LAYOUT_UNDEFINED;
			undefinedResult.barrierSync = D3D12_BARRIER_SYNC_NONE;
			undefinedResult.queueTypes = resourceState.queueTypes;
			return undefinedResult;
		}

		if (resourceState.isPresent())
		{
			ResourceBarrierUsageStates undefinedResult;
			undefinedResult.accessState = D3D12_BARRIER_ACCESS_COMMON;
			undefinedResult.layoutState = D3D12_BARRIER_LAYOUT_PRESENT;
			undefinedResult.queueLocalLayoutState = D3D12_BARRIER_LAYOUT_PRESENT;
			undefinedResult.barrierSync = D3D12_BARRIER_SYNC_ALL;
			undefinedResult.queueTypes = resourceState.queueTypes;
			return undefinedResult;
		}

		D3D12_BARRIER_ACCESS barrierAccess = D3D12_BARRIER_ACCESS_COMMON;
		D3D12_BARRIER_SYNC resultSync = D3D12_BARRIER_SYNC_NONE;

		IterateResourceUsages(resourceState.resourceUsage, [&](EResourceUsage usage)
		{
			switch (usage)
			{
			case graphics_backend::EResourceUsage::eConstantBuffer:
				resultSync |= DetermingShaderStageSync(resourceState.shaderStages);
				barrierAccess |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
				break;
			case graphics_backend::EResourceUsage::eShaderResource:
				resultSync |= DetermingShaderStageSync(resourceState.shaderStages);
				barrierAccess |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
				break;
			case graphics_backend::EResourceUsage::eShaderUnorderedAccess:
				resultSync |= DetermingShaderStageSync(resourceState.shaderStages);
				barrierAccess |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
				break;
			case graphics_backend::EResourceUsage::eRenderTarget:
				resultSync |= D3D12_BARRIER_SYNC_RENDER_TARGET;
				barrierAccess |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
				break;
			case graphics_backend::EResourceUsage::eDepthStencilTarget:
				resultSync |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
				if (resourceState.Read())
				{
					barrierAccess |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
				}
				if (resourceState.Write())
				{
					barrierAccess |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
				}
				break;
			case graphics_backend::EResourceUsage::eVertexInput:
				resultSync |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
				barrierAccess |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
				break;
			case graphics_backend::EResourceUsage::eIndexInput:
				resultSync |= D3D12_BARRIER_SYNC_INDEX_INPUT;
				barrierAccess |= D3D12_BARRIER_ACCESS_INDEX_BUFFER;
				break;
			case graphics_backend::EResourceUsage::eCopy:
				resultSync |= D3D12_BARRIER_SYNC_COPY;
				if (resourceState.Read())
				{
					barrierAccess |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
				}
				if (resourceState.Write())
				{
					barrierAccess |= D3D12_BARRIER_ACCESS_COPY_DEST;
				}
				break;
			default:
				break;
			}
		});

		//TODO: Check Validity: access state and layout state compatible?


		ResourceBarrierUsageStates result;
		result.accessState = barrierAccess;
		DetermineLayoutByAccessFlags(resourceState.isImage
			, resourceState.queueTypes
			, barrierAccess
			, result.layoutState
			, result.queueLocalLayoutState
		);
		result.barrierSync = resultSync;
		result.queueTypes = resourceState.queueTypes;
		//if (barrierAccess & D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ)
		//{
		//	CA_LOG("Found {}/{}/{}", barrierAccess, result.layoutState, result.queueLocalLayoutState);
		//}
		return result;
	}

	struct TextureResourceViews
	{
		struct ResourceViews
		{
			DescriptorAllocation srv;
			DescriptorAllocation uav;
			DescriptorAllocation rtv;
			DescriptorAllocation dsv;
		};
		castl::unordered_map<GPUTextureView, ResourceViews> resourceViews;

		DescriptorAllocation const& GetResourceView(EResourceViewType viewType, GPUTextureView const& textureView) const;

		//DescriptorAllocation const& EnsureResourceView()

		DescriptorAllocation const& EnsureSRV_NoLock(RenderBackend_D3D12* app
			, CPUDescriptorAllocatorSet& allocatorSet
			, ID3D12Resource* pResource
			, GPUTextureDescriptor const& desc
			, GPUTextureView const& textureView);
		DescriptorAllocation const& EnsureUAV_NoLock(RenderBackend_D3D12* app
			, CPUDescriptorAllocatorSet& allocatorSet
			, ID3D12Resource* pResource
			, GPUTextureDescriptor const& desc
			, GPUTextureView const& textureView);
		DescriptorAllocation const& EnsureRTV_NoLock(RenderBackend_D3D12* app
			, CPUDescriptorAllocatorSet& allocatorSet
			, ID3D12Resource* pResource
			, GPUTextureDescriptor const& desc
			, GPUTextureView const& textureView);
		DescriptorAllocation const& EnsureDSV_NoLock(RenderBackend_D3D12* app
			, CPUDescriptorAllocatorSet& allocatorSet
			, ID3D12Resource* pResource
			, GPUTextureDescriptor const& desc
			, GPUTextureView const& textureView);

		DescriptorAllocation const& EnsureSRV(RenderBackend_D3D12* app
			, CPUDescriptorAllocatorSet& allocatorSet
			, castl::shared_mutex& inMutex
			, ID3D12Resource* pResource
			, GPUTextureDescriptor const& desc
			, GPUTextureView const& textureView);
		DescriptorAllocation const& EnsureUAV(RenderBackend_D3D12* app
			, CPUDescriptorAllocatorSet& allocatorSet
			, castl::shared_mutex& inMutex
			, ID3D12Resource* pResource
			, GPUTextureDescriptor const& desc
			, GPUTextureView const& textureView);
		DescriptorAllocation const& EnsureRTV(RenderBackend_D3D12* app
			, CPUDescriptorAllocatorSet& allocatorSet
			, castl::shared_mutex& inMutex
			, ID3D12Resource* pResource
			, GPUTextureDescriptor const& desc
			, GPUTextureView const& textureView);
		DescriptorAllocation const& EnsureDSV(RenderBackend_D3D12* app
			, CPUDescriptorAllocatorSet& allocatorSet
			, castl::shared_mutex& inMutex
			, ID3D12Resource* pResource
			, GPUTextureDescriptor const& desc
			, GPUTextureView const& textureView);
	};

	struct BufferResourceViews
	{
		DescriptorAllocation srv;
		DescriptorAllocation uav;
		DescriptorAllocation cbv;

		DescriptorAllocation const& EnsureResourceView_NoLock(EResourceViewType viewType
			, RenderBackend_D3D12* app
			, ID3D12Resource* pResource
			, CPUDescriptorAllocatorSet& allocatorSet
			, GPUBufferDescriptor const& resourceDesc
		);
	};

	struct BufferResourceAllocationInfo
	{
	public:
		GPUBufferDescriptor resourceDesc;
		AliasedGPUResource gpuResource;
		EResourceUsageFlags usages;
		ID3D12Resource* pResource;

		BufferResourceViews bufferResourceView;
		DescriptorAllocation EnsureResourceView(EResourceViewType viewType
			, RenderBackend_D3D12* app
			, CPUDescriptorAllocatorSet& allocatorSet);

	};


	struct TextureResourceAllocationInfo
	{
	public:
		GPUTextureDescriptor resourceDesc;
		AliasedGPUResource gpuResource;
		EResourceUsageFlags usages;
		ID3D12Resource* pResource;

		TextureResourceViews textureResourceViews;

		DescriptorAllocation EnsureResourceView(EResourceViewType viewType
			, RenderBackend_D3D12* app
			, CPUDescriptorAllocatorSet& allocatorSet
			, GPUTextureView const& textureView);
	};

	struct BufferResourceInfo
	{
		ID3D12Resource* pResource;
		GPUBufferDescriptor resourceDesc;
	};

	struct TextureResourceInfo
	{
		ID3D12Resource* pResource;
		GPUTextureDescriptor resourceDesc;
	};

	class GPUConstantBufferManager;
	class D3D12GraphLocalResourceManager : public D3D12SubobjectBase
	{
	public:
		D3D12GraphLocalResourceManager(RenderBackend_D3D12* app);
		void SetAllocator(AliasedMemoryAllocator* allocator);
		void AddTexture(ImageHandle const& imageHandle
			, GPUTextureDescriptor const& resourceDesc);
		void AddTexture(ImageHandle const& imageHandle
			, GPUTextureDescriptor const& resourceDesc
			, GPUTextureView const& textureView);
		void AddBuffer(BufferHandle const& bufferHandle, GPUBufferDescriptor const& resourceDesc);
		void AllocateAliasedResources(uint32_t resourceBatchCount
			, castl::unordered_map<ImageHandle, ResourceUsageRangeData> const& imageLifeTimes
			, castl::unordered_map<BufferHandle, ResourceUsageRangeData> const& bufferLifeTimes
			, castl::unordered_map<D3D2ShaderStruct const*, CBufferUsageData> const& cbufferLifetimes
			, GPUConstantBufferManager& constantBufferManager
		);
		void CommitAliasedResources();
		void Reset();
		void Release()override
		{
			Reset();
		}
		DescriptorAllocation const& EnsureResourceView(
			ImageHandle const& imageHandle
			, EResourceViewType viewType
			, CPUDescriptorAllocatorSet& allocatorSet
			, GPUTextureView const& textureView);
		DescriptorAllocation const& EnsureResourceView(
			BufferHandle const& bufferHandle
			, EResourceViewType viewType
			, CPUDescriptorAllocatorSet& allocatorSet);

		ID3D12Resource* GetImageD3D12Resource(ImageHandle const& imageHandle) const;

		ID3D12Resource* GetBufferD3D12Resource(BufferHandle const& bufferHandle) const;

		TextureResourceInfo GetTextureResourceInfo(ImageHandle const& bufferHandle) const;
		BufferResourceInfo GetBufferResourceInfo(BufferHandle const& bufferHandle) const;

	private:

		TextureResourceAllocationInfo const* GetImageResource(ImageHandle const& imageHandle) const;
		TextureResourceAllocationInfo* GetImageResource(ImageHandle const& imageHandle);
		BufferResourceAllocationInfo* GetBufferResource(BufferHandle const& bufferHandle);
		BufferResourceAllocationInfo const* GetBufferResource(BufferHandle const& bufferHandle) const;
		
		castl::unordered_map<ImageHandle, TextureResourceAllocationInfo> imageHandleToResource;
		castl::unordered_map<BufferHandle, BufferResourceAllocationInfo> bufferHandleToResource;
		AliasedMemoryAllocator* p_AliasedAllocator;
	};

}