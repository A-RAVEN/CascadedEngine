#include "pch.h"
#include "FramebufferObject.h"

namespace graphics_backend
{
	void FramebufferObject::Create(FramebufferDescriptor const& framebufferDescriptor)
	{
		m_Width = framebufferDescriptor.width;
		m_Height = framebufferDescriptor.height;
		m_Layers = framebufferDescriptor.layers;
		mFramebuffer = GetDevice().createFramebuffer(vk::FramebufferCreateInfo{
			{}//vk::FramebufferCreateFlagBits::eImageless
				, framebufferDescriptor.renderpassObject->GetRenderPass()
				, framebufferDescriptor.renderImageViews
				, framebufferDescriptor.width
				, framebufferDescriptor.height
				, framebufferDescriptor.layers
		});
	}
	void FramebufferObject::Release()
	{
		GetDevice().destroyFramebuffer(mFramebuffer);
	}
}