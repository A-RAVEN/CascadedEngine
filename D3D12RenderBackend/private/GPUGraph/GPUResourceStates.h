#pragma once
#include "D3D12Includes.h"
#include <Utils/D3D12SubobjectBase.h>
#include <Common.h>
#include <CASTL/CAUnorderedMap.h>
#include <GPUGraph.h>
#include <ResourceManagment/MemoryManager.h>
#include <DescriptorManagment/GPUDescriptorHeap.h>

namespace graphics_backend
{
	struct D3D12TextureUsageState
	{
		D3D12_BARRIER_ACCESS accessState;
		D3D12_BARRIER_LAYOUT layoutState;
		D3D12_COMMAND_LIST_TYPE queueType;
	};

	struct D3D12BufferUsageState
	{
		D3D12_BARRIER_ACCESS accessState;
		D3D12_COMMAND_LIST_TYPE queueType;
	};

	struct D3D12PassResourceStates
	{
		castl::vector<castl::pair<ImageHandle, D3D12TextureUsageState>> textureUsageStates;
		castl::vector<castl::pair<BufferHandle, D3D12BufferUsageState>> bufferUsageStates;
	};

	struct TextureResourceTransition
	{
		D3D12_BARRIER_ACCESS sourceAccess;
		D3D12_BARRIER_ACCESS dstAccess;
		D3D12_BARRIER_LAYOUT sourceLayout;
		D3D12_BARRIER_LAYOUT destLayout;
		D3D12_COMMAND_LIST_TYPE sourceQueueType;
		D3D12_COMMAND_LIST_TYPE destQueueType;
	};

	struct BufferResourceTransition
	{
		D3D12_BARRIER_ACCESS sourceAccess;
		D3D12_BARRIER_ACCESS dstAccess;
		D3D12_COMMAND_LIST_TYPE sourceQueueType;
		D3D12_COMMAND_LIST_TYPE destQueueType;
	};


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

	class D3D12GraphLocalResourceManager : public D3D12SubobjectBase
	{
	public:
		D3D12GraphLocalResourceManager(RenderBackend_D3D12* app);
		void AddTexture(ImageHandle const& imageHandle
			, GPUTextureDescriptor const& resourceDesc
			, GPUTextureView const& textureView);
		void AddBuffer(BufferHandle const& bufferHandle, GPUBufferDescriptor const& resourceDesc);
		void AddGPUPassResourceStates(D3D12PassResourceStates const& states);
		void AllocateAliasedResources(uint32_t resourceBatchCount, castl::unordered_map<ImageHandle, castl::range<uint32_t>> imageLifeTimes,
			castl::unordered_map<BufferHandle, castl::range<uint32_t>> bufferLifeTimes);
		void PrepareResourceDescriptors(CPUDescriptorAllocatorSet& descriptorAllocators);
		TextureResourceAllocationInfo const* GetImageResource(ImageHandle const& imageHandle) const;
		BufferResourceAllocationInfo const* GetBufferResource(BufferHandle const& bufferHandle) const;
		castl::unordered_map<ImageHandle, TextureResourceAllocationInfo> imageHandleToResource;
		castl::unordered_map<BufferHandle, BufferResourceAllocationInfo> bufferHandleToResource;
		castl::vector<D3D12PassResourceStates> resourceStates;

		AliasedMemoryAllocator aliasedAllocator;
		uint32_t resourceIDCounter;
	};

}