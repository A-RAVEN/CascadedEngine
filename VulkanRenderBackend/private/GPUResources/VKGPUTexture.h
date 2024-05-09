#pragma once
#include <Platform.h>
#include <VulkanIncludes.h>
#include <GPUTexture.h>
#include "GPUResource.h"

namespace graphics_backend
{
	class VKGPUTexture : public GPUTexture, public GPUResource
	{
	public:
		VKGPUTexture(vk::Image image) {}
		constexpr vk::Image GetImage() const
		{
			return m_Image;
		}
		virtual GPUTextureDescriptor const& GetDescriptor() const override {return m_Descriptor;}
		virtual void SetName(castl::string const& name) override { m_Name = name; }
		virtual castl::string const& GetName() const override { return m_Name; }
	private:
		vk::Image m_Image;
		castl::string m_Name;
		GPUTextureDescriptor m_Descriptor;
	};
}