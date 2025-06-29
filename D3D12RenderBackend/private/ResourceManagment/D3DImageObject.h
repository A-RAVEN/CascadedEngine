#pragma once
#include <GPUTexture.h>
#include "GPUResource.h" 
#include <ResourceManagment/GPUResourceStates.h>
#include <CASTL/CAMutex.h>

namespace graphics_backend
{
	class D3DImageObject : public GPUTexture, public D3D12SubobjectBase
	{
	public:
		D3DImageObject(RenderBackend_D3D12* app);
		void Release();
		D3DImageObject& operator=(D3DImageObject&& other) noexcept = default;

		virtual GPUTextureDescriptor const& GetDescriptor() const override;
		virtual void SetName(castl::string const& name) override;
		virtual castl::string const& GetName() const override;

		void SetGPUResource(GPUResource&& resource);
		void SetDescriptor(GPUTextureDescriptor const& desc);
		ResourceState const& GetResourceState() const { return m_LastResourceState; }
		ResourceState& GetResourceState() { return m_LastResourceState; }
		GPUResource const& GetGPUResource() const { return m_Resource; }

		DescriptorAllocation const& EnsureSRV(GPUTextureView const& textureView);
		DescriptorAllocation const& EnsureUAV(GPUTextureView const& textureView);
		DescriptorAllocation const& EnsureRTV(GPUTextureView const& textureView);
		DescriptorAllocation const& EnsureDSV(GPUTextureView const& textureView);
	private:
		mutable castl::shared_mutex m_SharedMutex;
		GPUResource m_Resource;
		GPUTextureDescriptor m_Descriptor{};
		castl::string m_Name = { "" };
		ResourceState m_LastResourceState;
		TextureResourceViews m_CachedResourceViews;
	};
}