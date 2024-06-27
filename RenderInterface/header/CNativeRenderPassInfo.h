#pragma once
#include <CASTL/CAFunctional.h>
#include <CASTL/CAVector.h>
#include "Common.h"
#include "ShaderProvider.h"
#include "CPipelineStateObject.h"
#include "CVertexInputDescriptor.h"
#include "ShaderBindingBuilder.h"
#include "ShaderBindingSet.h"
#include "ShaderBindingSetHandle.h"
#include "CCommandList.h"
#include "IMeshInterface.h"

namespace graphics_backend
{
	struct CAttachmentInfo
	{
		ETextureFormat format = ETextureFormat::E_R8G8B8A8_UNORM;
		EAttachmentLoadOp loadOp = EAttachmentLoadOp::eLoad;
		EAttachmentStoreOp storeOp = EAttachmentStoreOp::eStore;
		EAttachmentLoadOp stencilLoadOp = EAttachmentLoadOp::eDontCare;
		EAttachmentStoreOp stencilStoreOp = EAttachmentStoreOp::eDontCare;
		EMultiSampleCount multiSampleCount = EMultiSampleCount::e1;

		auto operator<=>(const CAttachmentInfo&) const = default;

		constexpr static CAttachmentInfo Create(ETextureFormat format
			, EAttachmentLoadOp loadOp = EAttachmentLoadOp::eLoad
			, EAttachmentStoreOp storeOp = EAttachmentStoreOp::eStore
			, EMultiSampleCount multiSampleCount = EMultiSampleCount::e1)
		{
			CAttachmentInfo result{};
			result.format = format;
			result.loadOp = loadOp;
			result.storeOp = storeOp;
			result.stencilLoadOp = loadOp;
			result.stencilStoreOp = storeOp;
			result.multiSampleCount = multiSampleCount;
			return result;
		}

		constexpr static CAttachmentInfo CreateDS(ETextureFormat format
			, EAttachmentLoadOp loadOp = EAttachmentLoadOp::eLoad
			, EAttachmentStoreOp storeOp = EAttachmentStoreOp::eStore
			, EAttachmentLoadOp stencilLoadOp = EAttachmentLoadOp::eLoad
			, EAttachmentStoreOp stencilStoreOp = EAttachmentStoreOp::eStore
			, EMultiSampleCount multiSampleCount = EMultiSampleCount::e1)
		{
			CAttachmentInfo result{};
			result.format = format;
			result.loadOp = loadOp;
			result.storeOp = storeOp;
			result.stencilLoadOp = stencilLoadOp;
			result.stencilStoreOp = stencilStoreOp;
			result.multiSampleCount = multiSampleCount;
			return result;
		}
	};

	struct CSubpassInfo
	{
		castl::vector<uint32_t> colorAttachmentIDs;
		castl::vector<uint32_t> pixelInputAttachmentIDs;
		castl::vector<uint32_t> preserveAttachmentIDs;
		uint32_t depthAttachmentID;
		bool depthAttachmentReadOnly;

		auto operator<=>(const CSubpassInfo&) const = default;
	};

	struct CRenderPassInfo
	{
		castl::vector<CAttachmentInfo> attachmentInfos{};
		castl::vector<CSubpassInfo> subpassInfos{};
		auto operator<=>(const CRenderPassInfo&) const = default;
	};
}