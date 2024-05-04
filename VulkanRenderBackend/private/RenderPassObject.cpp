#include "pch.h"
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAUnorderedSet.h>
#include "RenderPassObject.h"
#include "InterfaceTranslator.h"
#include "ResourceUsageInfo.h"

template<>
struct hash_utils::is_contiguously_hashable<vk::SubpassDependency> : public castl::true_type {};

namespace graphics_backend
{

	RenderPassObject::RenderPassObject(CVulkanApplication& application) : VKAppSubObjectBaseNoCopy(application)
	{
	}

	void ExtractSubpassDependencies(RenderPassDescriptor const& descriptor)
	{
		auto& attachmentInfo = descriptor.renderPassInfo.attachmentInfos;
		auto& subpassInfos = descriptor.renderPassInfo.subpassInfos;

		castl::vector<uint32_t> tracking_attachment_ref_subpass(attachmentInfo.size());
		castl::fill(tracking_attachment_ref_subpass.begin()
			, tracking_attachment_ref_subpass.end()
			, INVALID_INDEX);

		for (size_t supassId = 0; supassId < subpassInfos.size(); ++supassId)
		{
		}
	}

	bool operator==(vk::SubpassDependency const& lhs, vk::SubpassDependency const& rhs) noexcept
	{
		return memcmp(&lhs, &rhs, sizeof(vk::SubpassDependency));
	}



	void ExtractAttachmentsInOutLayoutsAndSubpassDependencies(RenderPassDescriptor const& descriptor
		, castl::vector<castl::pair<vk::ImageLayout, vk::ImageLayout>>& inoutLayouts
		, castl::vector<vk::SubpassDependency>& inoutSubpassDependencies)
	{
		auto& attachmentInfo = descriptor.renderPassInfo.attachmentInfos;
		auto& subpassInfos = descriptor.renderPassInfo.subpassInfos;

		castl::vector<ResourceUsageFlags> tracking_attachment_usages(attachmentInfo.size());
		castl::fill(tracking_attachment_usages.begin(), tracking_attachment_usages.end(), ResourceUsage::eDontCare);

		inoutLayouts.resize(attachmentInfo.size());
		castl::fill(inoutLayouts.begin(), inoutLayouts.end(), castl::make_pair(vk::ImageLayout::eUndefined, vk::ImageLayout::eUndefined));


		//正在被引用的subpassid
		castl::vector<uint32_t> tracking_attachment_ref_subpass(attachmentInfo.size());
		castl::fill(tracking_attachment_ref_subpass.begin()
			, tracking_attachment_ref_subpass.end()
			, VK_SUBPASS_EXTERNAL);

		castl::unordered_set<vk::SubpassDependency, cacore::hash<vk::SubpassDependency>> dependencies{};

		auto iterate_set_attachment_usage = [
				&tracking_attachment_usages
				, &tracking_attachment_ref_subpass
				, &dependencies
				,&inoutLayouts]
		(uint32_t subpassId, size_t attachment_id, ResourceUsage new_usage)
		{
			auto& tracking_usage = tracking_attachment_usages[attachment_id];
			uint32_t& tracking_subpass_id = tracking_attachment_ref_subpass[attachment_id];
			auto& layout_pair = inoutLayouts[attachment_id];

			auto srcUsageInfo = GetUsageInfo(tracking_usage);
			auto dstUsageInfo = GetUsageInfo(new_usage);

			vk::SubpassDependency dependency{};
			dependency.srcSubpass = tracking_subpass_id;
			dependency.srcAccessMask = srcUsageInfo.m_UsageAccessFlags;
			dependency.srcStageMask = srcUsageInfo.m_UsageStageMask;
			dependency.dstSubpass = subpassId;
			dependency.dstAccessMask = dstUsageInfo.m_UsageAccessFlags;
			dependency.dstStageMask = dstUsageInfo.m_UsageStageMask;
			dependencies.insert(dependency);

			tracking_usage = new_usage;
			tracking_subpass_id = subpassId;
			layout_pair.second = dstUsageInfo.m_UsageImageLayout;
			if (layout_pair.first == vk::ImageLayout::eUndefined)
			{
				layout_pair.first = dstUsageInfo.m_UsageImageLayout;
			}
		};

		for (uint32_t supassId = 0; supassId < subpassInfos.size(); ++supassId)
		{
			auto& subpassInfo = subpassInfos[supassId];
			for(uint32_t attachmentId : subpassInfo.colorAttachmentIDs)
			{
				iterate_set_attachment_usage(supassId, attachmentId, ResourceUsage::eColorAttachmentOutput);
			}

			for(uint32_t attahmentId : subpassInfo.pixelInputAttachmentIDs)
			{
				iterate_set_attachment_usage(supassId, attahmentId, ResourceUsage::eFragmentRead);
			}

			if(subpassInfo.depthAttachmentID != INVALID_ATTACHMENT_INDEX)
			{
				assert(subpassInfo.depthAttachmentID < attachmentInfo.size());
				iterate_set_attachment_usage(supassId, subpassInfo.depthAttachmentID, ResourceUsage::eDepthStencilAttachment);
			}
		}

		inoutSubpassDependencies.resize(dependencies.size());
		castl::copy(dependencies.begin(), dependencies.end(), inoutSubpassDependencies.begin());
	}

	void RenderPassObject::Create(RenderPassDescriptor const& descriptor)
	{
		m_Descriptor = descriptor;
		auto& attachmentInfo = descriptor.renderPassInfo.attachmentInfos;
		auto& subpassInfos = descriptor.renderPassInfo.subpassInfos;

		m_AttachmentCounrt = attachmentInfo.size();
		m_SubpassCount = subpassInfos.size();

		castl::vector<vk::SubpassDependency> subpassDependencies{};
		ExtractAttachmentsInOutLayoutsAndSubpassDependencies(
			descriptor
			, m_AttachmentExternalLayouts
			, subpassDependencies);

		castl::vector<vk::AttachmentDescription> attachmentList{attachmentInfo.size()};
		for (size_t attachmentId = 0; attachmentId < attachmentList.size(); ++attachmentId)
		{
			auto& attachmentDesc = attachmentList[attachmentId];
			auto const& srcInfo = attachmentInfo[attachmentId];
			auto const& externalLayouts = m_AttachmentExternalLayouts[attachmentId];

			attachmentDesc.initialLayout = externalLayouts.first;
			attachmentDesc.finalLayout = externalLayouts.second;
			attachmentDesc.format = ETextureFormatToVkFotmat(srcInfo.format);
			attachmentDesc.samples = EMultiSampleCountToVkSampleCount(srcInfo.multiSampleCount);
			attachmentDesc.loadOp = EAttachmentLoadOpToVkLoadOp(srcInfo.loadOp);
			attachmentDesc.storeOp = EAttachmentStoreOpToVkStoreOp(srcInfo.storeOp);
			attachmentDesc.stencilLoadOp = EAttachmentLoadOpToVkLoadOp(srcInfo.stencilLoadOp);
			attachmentDesc.stencilStoreOp = EAttachmentStoreOpToVkStoreOp(srcInfo.stencilStoreOp);
		}

		castl::vector<vk::SubpassDescription> subpassDescs{subpassInfos.size()};
		castl::vector<castl::vector<vk::AttachmentReference>> subpassAttachmentRefs{};
		castl::vector<castl::vector<uint32_t>> subpassPreserveAttachmentIDs{};
		castl::vector<vk::AttachmentReference> subpassDepthAttachmentRefs{};
		for (size_t subpassId = 0; subpassId < subpassDescs.size(); ++subpassId)
		{
			auto& subpassDesc = subpassDescs[subpassId];
			auto& srcInfo = subpassInfos[subpassId];

			subpassDesc.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;

			subpassAttachmentRefs.emplace_back(srcInfo.colorAttachmentIDs.size());
			auto& attachmentRefs = subpassAttachmentRefs.back();
			for (size_t refId = 0; refId < attachmentRefs.size(); ++refId)
			{
				auto& attachmentRef = attachmentRefs[refId];
				const uint32_t srcRefId = srcInfo.colorAttachmentIDs[refId];
				attachmentRef.attachment = srcRefId;
				attachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;
			};
			subpassDesc.setColorAttachments(attachmentRefs);

			subpassAttachmentRefs.emplace_back(srcInfo.pixelInputAttachmentIDs.size());
			auto& inputAttachmentRefs = subpassAttachmentRefs.back();
			for (size_t refId = 0; refId < inputAttachmentRefs.size(); ++refId)
			{
				auto& attachmentRef = inputAttachmentRefs[refId];
				const uint32_t srcRefId = srcInfo.pixelInputAttachmentIDs[refId];
				attachmentRef.attachment = srcRefId;
				attachmentRef.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
			};
			subpassDesc.setInputAttachments(inputAttachmentRefs);

			if(srcInfo.depthAttachmentID != INVALID_ATTACHMENT_INDEX)
			{
				subpassDepthAttachmentRefs.emplace_back();
				auto& depthAttachmentRef = subpassDepthAttachmentRefs.back();
				depthAttachmentRef.attachment = srcInfo.depthAttachmentID;
				depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
				subpassDesc.setPDepthStencilAttachment(&depthAttachmentRef);
			}

			if (!srcInfo.preserveAttachmentIDs.empty())
			{
				subpassPreserveAttachmentIDs.emplace_back(srcInfo.preserveAttachmentIDs.size());
				auto& preserveAttachmentIDs = subpassPreserveAttachmentIDs.back();
				for (size_t refId = 0; refId < preserveAttachmentIDs.size(); ++refId)
				{
					preserveAttachmentIDs[refId] = srcInfo.preserveAttachmentIDs[refId];
				};
				subpassDesc.setPreserveAttachments(preserveAttachmentIDs);
			}
		}

		vk::RenderPassCreateInfo renderpass_createInfo(
			{}
			, attachmentList
			, subpassDescs
			, subpassDependencies
		);
		m_RenderPass = GetDevice().createRenderPass(renderpass_createInfo);
	}

	void RenderPassObject::Release()
	{
		if (m_RenderPass != vk::RenderPass(nullptr))
		{
			GetDevice().destroyRenderPass(m_RenderPass);
			m_RenderPass = nullptr;
		}
	}


}