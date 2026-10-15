#pragma once
#include <CASTL/CAArray.h>
//#include <uhash.h>
#include "CodeGenMeta.h"
#include "Common.h"

// 也是哈希键（`HashObj<RectSate>`，出现在 GPUGraph 的视图矩形描述里）。
CA_REFLECT(serializable)
struct RectSate
{
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	auto operator<=>(const RectSate&) const = default;
};

CA_REFLECT(serializable)
struct RasterizerStates
{
public:
	bool enableDepthClamp = false;
	bool discardRasterization = false;
	ECullMode cullMode = ECullMode::eNone;
	EFrontFace frontFace = EFrontFace::eCounterClockWise;
	EPolygonMode polygonMode = EPolygonMode::eFill;
	float lineWidth = 1.0f;

	auto operator<=>(const RasterizerStates&) const = default;

	constexpr static RasterizerStates CullOff()
	{
		RasterizerStates states{};
		states.cullMode = ECullMode::eNone;
		return states;
	}

	constexpr static RasterizerStates CullBack()
	{
		RasterizerStates states{};
		states.cullMode = ECullMode::eBack;
		return states;
	}

	constexpr static RasterizerStates CullFront()
	{
		RasterizerStates states{};
		states.cullMode = ECullMode::eFront;
		return states;
	}
};

CA_REFLECT(serializable)
struct DepthStencilStates
{
	CA_REFLECT(serializable)
	struct StencilStates
	{
		EStencilOp failOp = EStencilOp::eKeep;
		EStencilOp passOp = EStencilOp::eKeep;
		EStencilOp depthFailOp = EStencilOp::eKeep;
		ECompareOp compareOp = ECompareOp::eAlways;
		uint32_t compareMask = 0u;
		uint32_t writeMask = 0u;
		uint32_t reference = 0u;

		auto operator<=>(const StencilStates&) const = default;
	};

	bool depthTestEnable = false;
	bool depthWriteEnable = false;
	ECompareOp depthCompareOp = ECompareOp::eGEqual;

	bool stencilTestEnable = false;
	StencilStates stencilStateFront = {};
	StencilStates stencilStateBack = {};

	auto operator<=>(const DepthStencilStates&) const = default;

	static DepthStencilStates NormalOpaque()
	{
		DepthStencilStates states;
		states.depthTestEnable = true;
		states.depthWriteEnable = true;
		states.depthCompareOp = ECompareOp::eLEqual;
		return states;
	}
};

CA_REFLECT(serializable)
struct SingleColorAttachmentBlendStates
{
	bool blendEnable = false;
	EColorChannelMaskFlags channelMask = EColorChannelMask::eRGBA;
	EBlendFactor sourceColorBlendFactor = EBlendFactor::eOne;
	EBlendFactor destColorBlendFactor = EBlendFactor::eZero;
	EBlendFactor sourceAlphaBlendFactor = EBlendFactor::eOne;
	EBlendFactor destAlphaBlendFactor = EBlendFactor::eZero;
	EBlendOp colorBlendOp = EBlendOp::eAdd;
	EBlendOp alphaBlendOp = EBlendOp::eAdd;

	auto operator<=>(const SingleColorAttachmentBlendStates&) const = default;

	static SingleColorAttachmentBlendStates AlphaTransparent()
	{
		SingleColorAttachmentBlendStates states;
		states.blendEnable = true;
		states.sourceColorBlendFactor = EBlendFactor::eSrcAlpha;
		states.destColorBlendFactor = EBlendFactor::eOneMinusSrcAlpha;
		return states;
	}
};

CA_REFLECT(serializable)
struct ColorAttachmentsBlendStates
{
	castl::array<SingleColorAttachmentBlendStates, 8> attachmentBlendStates = {};

	auto operator<=>(const ColorAttachmentsBlendStates&) const = default;

	static ColorAttachmentsBlendStates AlphaTransparent()
	{
		ColorAttachmentsBlendStates states;
		for (uint32_t i = 0; i < 8; ++i)
		{
			states.attachmentBlendStates[i] = SingleColorAttachmentBlendStates::AlphaTransparent();
		}
		return states;
	}
};

/// <summary>
/// Pipeline States That May Share During One Pass
/// Pure Data Struct, Not The Real One Used In Backend API
/// </summary>
CA_REFLECT(serializable)
class CPipelineStateObject
{
public:
	DepthStencilStates depthStencilStates = {};
	RasterizerStates rasterizationStates = {};
	ColorAttachmentsBlendStates colorAttachments = {};
	EMultiSampleCount msCount = EMultiSampleCount::e1;

	auto operator<=>(const CPipelineStateObject&) const = default;
};

// 这七个类型的生成物都在这里 include —— **包括 `CPipelineStateObject` 自己的**。
//
// ⚠ 原来这里只 include `RectSate` 一份，其余"交给消费方自己 include"，那是错的：只要别处用到
// `cacore::HashObj<CPipelineStateObject>`（例如 `Interface/RenderInterface/header/GPUGraph.h` 里的
// `PipelineDescData::m_PipelineStates`），生成代码就会顺着成员一路哈希下去，
// `CGHashContainerValue(h, obj.depthStencilStates)` 会要 `DepthStencilStates` 的特化 ——
// 而那个特化只有"消费方恰好 include 了它"才在。实测（`require-explicit-hash-source` 的探针）：
// 关掉运行时的隐式兜底后，**37 个编译单元**因此编不过，其中 **32 个**缺的正是这几个成员类型的特化；
// 而这 37 个在改之前**编得过**，靠的是"平凡可拷贝就悄悄按整块字节"那条兜底。
#include "RectSate.codegen.h"
#include "CPipelineStateObject.codegen.h"
#include "DepthStencilStates.codegen.h"
#include "RasterizerStates.codegen.h"
#include "StencilStates.codegen.h"
#include "SingleColorAttachmentBlendStates.codegen.h"
#include "ColorAttachmentsBlendStates.codegen.h"
