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
		constexpr vk::Image GetImage() const
		{
			return m_Image;
		}
		virtual GPUTextureDescriptor const& GetDescriptor() const override {return m_Descriptor;}
		virtual void SetName(castl::string const& name) override { m_Name = name; }
		virtual castl::string const& GetName() const override { return m_Name; }
		bool Initialized() const
		{
			return m_Image != vk::Image{nullptr};
		}
		void SetImage(vk::Image image)
		{
			m_Image = image;
		}
		void SetDescriptor(GPUTextureDescriptor const& descriptor)
		{
			m_Descriptor = descriptor;
		}
	private:
		vk::Image m_Image = {};
		GPUTextureDescriptor m_Descriptor = {};
		castl::string m_Name = {""};
	};
}