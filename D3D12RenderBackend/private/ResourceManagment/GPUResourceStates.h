#pragma once
#include "D3D12Includes.h"
#include <Utils/D3D12SubobjectBase.h>
#include <Common.h>
#include <CASTL/CAUnorderedMap.h>
#include <GPUGraph.h>
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <ResourceManagment/MemoryManager.h>
#include <DescriptorManagment/GPUDescriptorHeap.h>

namespace graphics_backend
{
	enum class EResourceUsage : uint32_t
	{
		eShaderResource = 1 << 0,
		eShaderUnorderedAccess = 1 << 1,
		eRenderTarget = 1 << 2,
		eVertexInput = 1 << 3,
		eIndexInput = 1 << 4,
		eCopy = 1 << 5,
		eConstantBuffer = 1 << 6,
		eDepthStencilTarget = 1 << 7,
		eInitialized = 1 << 8,
		eBitMax = 9,
	};
	using EResourceUsageFlags = uenum::EnumFlags<EResourceUsage>;

	enum class EGPUQueueType : uint32_t
	{
		eNone = 0,
		eDirect = 1 << 0,
		eCompute = 1 << 1,
		eCopy = 1 << 2,
	};
	using EGPUQueueTypeFlags = uenum::EnumFlags<EGPUQueueType>;

	struct ResourceBarrierUsageStates
	{
		D3D12_BARRIER_ACCESS accessState;
		D3D12_BARRIER_LAYOUT layoutState;
		D3D12_BARRIER_SYNC barrierSync;
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

		bool isUndefined() const
		{
			return resourceUsage == EResourceUsage::eInitialized;
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
		bool Compatible(ResourceState const& other) const
		{
			return (resourceAccess == other.resourceAccess);
		}
		void Combine(ResourceState const& other)
		{
			CA_ASSERT_BREAK(resourceAccess == other.resourceAccess, "Resource Access Not Compatible");
			shaderStages |= other.shaderStages;
			resourceUsage |= other.resourceUsage;
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
	struct ResourceUsageRangeData
	{
		ResourceUsageRange lifeTime;

		struct BatchAndState
		{
			uint32_t batchID;
			ResourceState state;
		};
		castl::vector<BatchAndState> states;

		void Expand(uint32_t passID, ResourceState const& resourceState)
		{
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
		return result;
	}

	static D3D12_RESOURCE_STATES DetermingResourceStates(ResourceState const& resourceState)
	{


		D3D12_RESOURCE_STATES resultStates = D3D12_RESOURCE_STATE_COMMON;
		const EShaderTypeFlags nonPixelShaderStage =
			EShaderTypeFlags(EShaderTypeMask::eAllVertex) | EShaderTypeMask::eAllRaytracing | EShaderTypeMask::eComp;
		IterateResourceUsages(resourceState.resourceUsage, [&](EResourceUsage usage)
		{
			switch (usage)
			{
			case graphics_backend::EResourceUsage::eConstantBuffer:
				resultStates |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
				break;
			case graphics_backend::EResourceUsage::eShaderResource:
				if (resourceState.shaderStages & EShaderTypeMask::eFrag)
				{
					resultStates |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
				}
				if (resourceState.shaderStages & nonPixelShaderStage)
				{
					resultStates |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
				}
				break;
			case graphics_backend::EResourceUsage::eShaderUnorderedAccess:
				resultStates |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

				break;
			case graphics_backend::EResourceUsage::eRenderTarget:
				resultStates |= D3D12_RESOURCE_STATE_RENDER_TARGET;
				break;
			case graphics_backend::EResourceUsage::eDepthStencilTarget:
				if (resourceState.Read())
				{
					resultStates |= D3D12_RESOURCE_STATE_DEPTH_READ;
				}
				if (resourceState.Write())
				{
					resultStates |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
				}
				break;
			case graphics_backend::EResourceUsage::eVertexInput:
				resultStates |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
				break;
			case graphics_backend::EResourceUsage::eIndexInput:
				resultStates |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
				break;
			case graphics_backend::EResourceUsage::eCopy:
				if (resourceState.ReadOnly())
				{
					resultStates |= D3D12_RESOURCE_STATE_COPY_SOURCE;
				}
				else
				{
					resultStates |= D3D12_RESOURCE_STATE_COPY_DEST;
				}
				break;
			default:
				break;
			}
		});

		return resultStates;
	}

	static ResourceBarrierUsageStates DetermineResourceBarrierUsageStates(ResourceState const& resourceState)
	{
		if (resourceState.isUndefined())
		{
			D3D12_BARRIER_ACCESS access = D3D12_BARRIER_ACCESS_NO_ACCESS;
			D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;
			D3D12_BARRIER_LAYOUT layout = D3D12_BARRIER_LAYOUT_UNDEFINED;
			ResourceBarrierUsageStates undefinedResult;
			undefinedResult.accessState = access;
			undefinedResult.layoutState = layout;
			undefinedResult.barrierSync = sync;
			return undefinedResult;
		}

		D3D12_BARRIER_ACCESS barrierAccess = D3D12_BARRIER_ACCESS_COMMON;
		D3D12_BARRIER_SYNC resultSync = D3D12_BARRIER_SYNC_NONE;
		D3D12_BARRIER_LAYOUT barrierLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;

		auto setBarrierLayout = [&](D3D12_BARRIER_LAYOUT layout)
		{
			if (barrierLayout == D3D12_BARRIER_LAYOUT_UNDEFINED)
			{
				barrierLayout = layout;
			}
			else
			{
				barrierLayout = D3D12_BARRIER_LAYOUT_COMMON;
			}
		};

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
				setBarrierLayout(D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
				break;
			case graphics_backend::EResourceUsage::eShaderUnorderedAccess:
				resultSync |= DetermingShaderStageSync(resourceState.shaderStages);
				barrierAccess |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
				setBarrierLayout(D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
				break;
			case graphics_backend::EResourceUsage::eRenderTarget:
				resultSync |= D3D12_BARRIER_SYNC_RENDER_TARGET;
				barrierAccess |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
				setBarrierLayout(D3D12_BARRIER_LAYOUT_RENDER_TARGET);
				break;
			case graphics_backend::EResourceUsage::eDepthStencilTarget:
				resultSync |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
				if (resourceState.Read())
				{
					barrierAccess |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
					setBarrierLayout(D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ);
				}
				if (resourceState.Write())
				{
					barrierAccess |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
					setBarrierLayout(D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
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
				if (resourceState.ReadOnly())
				{
					barrierAccess |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
					setBarrierLayout(D3D12_BARRIER_LAYOUT_COPY_SOURCE);
				}
				else
				{
					barrierAccess |= D3D12_BARRIER_ACCESS_COPY_DEST;
					setBarrierLayout(D3D12_BARRIER_LAYOUT_COPY_SOURCE);
				}
				break;
			default:
				break;
			}
		});

		ResourceBarrierUsageStates result;
		result.accessState = barrierAccess;
		result.layoutState = barrierLayout;
		result.barrierSync = resultSync;
		return result;
	}

	struct BufferResourceAllocationInfo
	{
		GPUBufferDescriptor resourceDesc;
		AliasedGPUResource gpuResource;
		D3D12_BARRIER_ACCESS access;
		DescriptorAllocation srv;
		DescriptorAllocation uav;
		DescriptorAllocation cbv;
	};

	struct TextureResourceAllocationInfo
	{
		GPUTextureDescriptor resourceDesc;
		AliasedGPUResource gpuResource;
		D3D12_BARRIER_ACCESS access;

		struct ResourceViews
		{
			DescriptorAllocation srv;
			DescriptorAllocation uav;
			DescriptorAllocation rtv;
			DescriptorAllocation dsv;
		};
		castl::unordered_map<GPUTextureView, ResourceViews> resourceViews;
	};

	class GPUConstantBufferManager;
	class D3D12GraphLocalResourceManager : public D3D12SubobjectBase
	{
	public:
		D3D12GraphLocalResourceManager(RenderBackend_D3D12* app);
		void AddTexture(ImageHandle const& imageHandle
			, GPUTextureDescriptor const& resourceDesc
			, GPUTextureView const& textureView);
		void AddBuffer(BufferHandle const& bufferHandle, GPUBufferDescriptor const& resourceDesc);
		//void AddGPUPassResourceStates(D3D12PassResourceStates const& states);
		void AllocateAliasedResources(uint32_t resourceBatchCount
			, castl::unordered_map<ImageHandle, ResourceUsageRangeData> const& imageLifeTimes
			, castl::unordered_map<BufferHandle, ResourceUsageRangeData> const& bufferLifeTimes
			, castl::unordered_map<D3D2ShaderStruct const*, ResourceUsageRange> const& cbufferLifetimes
			, GPUConstantBufferManager& constantBufferManager
		);
		void PrepareResourceDescriptors(CPUDescriptorAllocatorSet& descriptorAllocators);
		TextureResourceAllocationInfo const* GetImageResource(ImageHandle const& imageHandle) const;
		BufferResourceAllocationInfo const* GetBufferResource(BufferHandle const& bufferHandle) const;
		castl::unordered_map<ImageHandle, TextureResourceAllocationInfo> imageHandleToResource;
		castl::unordered_map<BufferHandle, BufferResourceAllocationInfo> bufferHandleToResource;
		//castl::vector<D3D12PassResourceStates> resourceStates;

		AliasedMemoryAllocator aliasedAllocator;
		uint32_t resourceIDCounter;
	};

}