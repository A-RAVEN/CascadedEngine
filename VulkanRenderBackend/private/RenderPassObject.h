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
		RenderPassDescriptor const* GetDescriptor() const { return p_Descriptor; }
	private:
		RenderPassDescriptor const* p_Descriptor;
		vk::RenderPass m_RenderPass = nullptr;
		castl::vector<castl::pair<vk::ImageLayout, vk::ImageLayout>> m_AttachmentExternalLayouts;
	};

	using RenderPassObjectDic = HashPool<RenderPassDescriptor, RenderPassObject>;
}