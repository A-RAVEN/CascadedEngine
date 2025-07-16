#pragma once
#include <GPUBuffer.h>
#include "GPUResource.h" 
#include <ResourceManagment/GPUResourceStates.h>

namespace graphics_backend
{
	class D3DBufferObject : public GPUBuffer, public D3D12SubobjectBase
	{
	public:
		D3DBufferObject(RenderBackend_D3D12* app);
		D3DBufferObject& operator=(D3DBufferObject&& other) noexcept = default;

		virtual GPUBufferDescriptor const& GetDescriptor() const override;
		virtual void SetName(castl::string const& name) override;
		virtual castl::string const& GetName() const override;

		void SetGPUResource(GPUResource&& resource);
		void SetDescriptor(GPUBufferDescriptor const& desc);
		ResourceState const& GetResourceState() const { return m_LastResourceState; }
		ResourceState& GetResourceState() { return m_LastResourceState; }
		GPUResource const& GetGPUResource() const
		{
			return m_Resource;
		}
		DescriptorAllocation const& EnsureResourceView(EResourceViewType viewType);
	private:
		GPUResource m_Resource;
		GPUBufferDescriptor m_Descriptor{};
		castl::string m_Name = { "" };
		ResourceState m_LastResourceState;

		BufferResourceViews m_CachedResourceViews;

	};
}