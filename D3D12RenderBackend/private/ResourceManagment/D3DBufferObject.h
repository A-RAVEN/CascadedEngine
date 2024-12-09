#pragma once
#include <GPUBuffer.h>
#include "GPUResource.h" 

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
	private:
		GPUResource m_Resource;
		GPUBufferDescriptor m_Descriptor{};
		castl::string m_Name = { "" };
	};
}