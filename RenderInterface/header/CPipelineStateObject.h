#pragma once
#include <CASTL/CAArray.h>
//#include <uhash.h>
#include <Reflection.h>
#include "Common.h"

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

struct DepthStencilStates
{
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
class CPipelineStateObject
{
public:
	DepthStencilStates depthStencilStates = {};
	RasterizerStates rasterizationStates = {};
	ColorAttachmentsBlendStates colorAttachments = {};
	EMultiSampleCount msCount = EMultiSampleCount::e1;

	auto operator<=>(const CPipelineStateObject&) const = default;
};

CA_REFLECTION(RasterizerStates
	, enableDepthClamp
	, discardRasterization
	, cullMode
	, frontFace
	, polygonMode
	, lineWidth);
CA_REFLECTION(DepthStencilStates::StencilStates
	, failOp
	, passOp
	, depthFailOp
	, compareOp
	, compareMask
	, writeMask
	, reference);
CA_REFLECTION(DepthStencilStates
	, depthTestEnable
	, depthWriteEnable
	, depthCompareOp
	, stencilTestEnable
	, stencilStateFront
	, stencilStateBack);
CA_REFLECTION(SingleColorAttachmentBlendStates
	, blendEnable
	, channelMask
	, sourceColorBlendFactor
	, destColorBlendFactor
	, sourceAlphaBlendFactor
	, destAlphaBlendFactor
	, colorBlendOp
	, alphaBlendOp);
CA_REFLECTION(ColorAttachmentsBlendStates, attachmentBlendStates);
CA_REFLECTION(CPipelineStateObject, depthStencilStates, rasterizationStates, colorAttachments, msCount);