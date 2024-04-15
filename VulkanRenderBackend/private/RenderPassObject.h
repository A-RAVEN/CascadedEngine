#pragma once
#include <uhash.h>
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

		template <class HashAlgorithm>
		friend void hash_append(HashAlgorithm& h, RenderPassDescriptor const& renderpass_desc) noexcept
		{
			hash_append(h, renderpass_desc.renderPassInfo);
		}
	};

	class RenderPassObject : public VKAppSubObjectBaseNoCopy
	{
	public:
		RenderPassObject(CVulkanApplication& application);
		void Create(RenderPassDescriptor const& descriptor);
		virtual void Release() override;
		vk::RenderPass GetRenderPass() const { return m_RenderPass; }
		uint32_t GetAttachmentCount() const { return m_AttachmentCounrt; }
		uint32_t GetSubpassCount() const { return m_SubpassCount; }
		RenderPassDescriptor const* GetDescriptor() const { return m_Descriptor; }
	private:
		RenderPassDescriptor const* m_Descriptor = nullptr;
		uint32_t m_AttachmentCounrt = 0;
		uint32_t m_SubpassCount = 0;
		vk::RenderPass m_RenderPass = nullptr;
		castl::vector<castl::pair<vk::ImageLayout, vk::ImageLayout>> m_AttachmentExternalLayouts;
	};

	using RenderPassObjectDic = HashPool<RenderPassDescriptor, RenderPassObject>;
}