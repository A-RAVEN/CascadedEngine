#pragma once
#include <CASTL/CAVector.h>
#include "VulkanIncludes.h"
#include "VulkanApplicationSubobjectBase.h"
#include "RenderPassObject.h"

namespace graphics_backend
{
	struct FramebufferDescriptor
	{
	public:
		castl::vector<vk::ImageView> renderImageViews;
		castl::shared_ptr<RenderPassObject> renderpassObject = nullptr;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t layers = 0;

		bool operator==(FramebufferDescriptor const& rhs) const
		{
			return renderImageViews == rhs.renderImageViews
				&& renderpassObject == rhs.renderpassObject;
		}

		template <class HashAlgorithm>
		friend void hash_append(HashAlgorithm& h, FramebufferDescriptor const& x) noexcept
		{
			hash_append(h, x.renderImageViews);
			hash_append(h, reinterpret_cast<size_t>(x.renderpassObject.get()));
		}
	};

	//TODO: Move To FrameBound Manager
	class FramebufferObject final : public VKAppSubObjectBaseNoCopy
	{
	public:
		FramebufferObject(CVulkanApplication& owner) : VKAppSubObjectBaseNoCopy(owner) {};
		void Create(FramebufferDescriptor const& framebufferDescriptor);
		void Release();
		vk::Framebuffer const& GetFramebuffer() const { return mFramebuffer; }
		uint32_t GetWidth() const 
		{
			return m_Width;
		}
		uint32_t GetHeight() const
		{
			return m_Height;
		}
	private:
		vk::Framebuffer mFramebuffer;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_Layers = 0;
	};

	using FramebufferObjectDic = HashPool<FramebufferDescriptor, FramebufferObject>;
}