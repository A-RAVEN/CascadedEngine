#pragma once
#include <CNativeRenderPassInfo.h>
#include "VulkanIncludes.h"
#include "VulkanApplicationSubobjectBase.h"
#include "HashPool.h"

namespace graphics_backend
{
	struct RenderPassDescriptor
	{
		CRenderPassInfo renderPassInfo{};

		bool operator==(RenderPassDescriptor const& other) const noexcept
		{
			bool result = (renderPassInfo == other.renderPassInfo);
			return result;
		}
	};

	class RenderPassObject : public VKAppSubObjectBaseNoCopy
	{
	public:
		RenderPassObject(CVulkanApplication& application);
		void Create(RenderPassDescriptor const& descriptor);
		void Release();
		vk::RenderPass GetRenderPass() const { return m_RenderPass; }
		uint32_t GetAttachmentCount() const { return m_AttachmentCounrt; }
		uint32_t GetSubpassCount() const { return m_SubpassCount; }
		RenderPassDescriptor const* GetDescriptor() const { return &m_Descriptor; }
	private:
		RenderPassDescriptor m_Descriptor;
		uint32_t m_AttachmentCounrt = 0;
		uint32_t m_SubpassCount = 0;
		vk::RenderPass m_RenderPass = nullptr;
		castl::vector<castl::pair<vk::ImageLayout, vk::ImageLayout>> m_AttachmentExternalLayouts;
	};

	using RenderPassObjectDic = HashPool<RenderPassDescriptor, RenderPassObject>;
}