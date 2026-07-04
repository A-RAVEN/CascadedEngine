#pragma once
#include <Common.h>
#include <Utils/VulkanIncludes.h>

namespace graphics_backend
{
	constexpr vk::PrimitiveTopology ETopologyToVkTopology(ETopology inTopology)
	{
		switch (inTopology)
		{
		case ETopology::eTriangleList: return vk::PrimitiveTopology::eTriangleList;
		case ETopology::eTriangleStrip: return vk::PrimitiveTopology::eTriangleStrip;
		case ETopology::ePointList: return vk::PrimitiveTopology::ePointList;
		case ETopology::eLineList: return vk::PrimitiveTopology::eLineList;
		case ETopology::eLineStrip: return vk::PrimitiveTopology::eLineStrip;
		default: return vk::PrimitiveTopology::eTriangleList;
		}
	}

	constexpr vk::PolygonMode EPolygonModeToVkPolygonMode(EPolygonMode inPolygonMode)
	{
		switch (inPolygonMode)
		{
		case EPolygonMode::eFill: return vk::PolygonMode::eFill;
		case EPolygonMode::eLine: return vk::PolygonMode::eLine;
		case EPolygonMode::ePoint: return vk::PolygonMode::ePoint;
		default: return vk::PolygonMode::eFill;
		}
	}

	constexpr vk::CullModeFlags ECullModeToVkCullModeFlags(ECullMode inCullMode)
	{
		switch (inCullMode)
		{
		case ECullMode::eBack: return vk::CullModeFlagBits::eBack;
		case ECullMode::eFront: return vk::CullModeFlagBits::eFront;
		case ECullMode::eNone: return vk::CullModeFlagBits::eNone;
		case ECullMode::eAll: return vk::CullModeFlagBits::eFrontAndBack;
		default: return vk::CullModeFlagBits::eNone;
		}
	}

	constexpr vk::FrontFace EFrontFaceToVkFrontFace(EFrontFace inFrontFace)
	{
		switch (inFrontFace)
		{
		case EFrontFace::eClockWise:
			return vk::FrontFace::eClockwise;
		case EFrontFace::eCounterClockWise:
			return vk::FrontFace::eCounterClockwise;
		default: return vk::FrontFace::eCounterClockwise;
		}
	}

	constexpr vk::ColorComponentFlags EColorChannelMaskToVkColorComponentFlags(EColorChannelMaskFlags inColorMask)
	{
		vk::ColorComponentFlags result = vk::ColorComponentFlags(0);
		if (inColorMask & EColorChannelMask::eR)
		{
			result |= vk::ColorComponentFlagBits::eR;
		}
		if (inColorMask & EColorChannelMask::eG)
		{
			result |= vk::ColorComponentFlagBits::eG;
		}
		if (inColorMask & EColorChannelMask::eB)
		{
			result |= vk::ColorComponentFlagBits::eB;
		}
		if (inColorMask & EColorChannelMask::eA)
		{
			result |= vk::ColorComponentFlagBits::eA;
		}
		return result;
	}
}
