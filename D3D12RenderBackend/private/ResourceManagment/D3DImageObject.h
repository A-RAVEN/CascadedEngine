#pragma once
#include <GPUTexture.h>
#include "GPUResource.h" 

namespace graphics_backend
{
	class D3DImageObject : public GPUTexture, public D3D12SubobjectBase
	{
	public:
		D3DImageObject(RenderBackend_D3D12* app);
		D3DImageObject& operator=(D3DImageObject&& other) noexcept = default;

		virtual GPUTextureDescriptor const& GetDescriptor() const override;
		virtual void SetName(castl::string const& name) override;
		virtual castl::string const& GetName() const override;

		void SetGPUResource(GPUResource&& resource);
		void SetDescriptor(GPUTextureDescriptor const& desc);
	private:
		GPUResource m_Resource;
		GPUTextureDescriptor m_Descriptor{};
		castl::string m_Name = { "" };
	};
}