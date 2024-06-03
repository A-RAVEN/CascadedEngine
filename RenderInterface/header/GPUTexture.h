#pragma once
#include <CASTL/CAAlgorithm.h>
#include "Common.h"

namespace graphics_backend
{
	struct GPUTextureDescriptor
	{
		ETextureFormat format;
		ETextureType textureType;
		ETextureAccessTypeFlags accessType;
		uint32_t width;
		uint32_t height;
		uint32_t layers;
		uint32_t mipLevels;
		EMultiSampleCount samples;

		auto operator<=>(const GPUTextureDescriptor&) const = default;

		static GPUTextureDescriptor Create(
			uint32_t width, uint32_t height
			, ETextureFormat format
			, ETextureAccessTypeFlags accessType
			, ETextureType textureType = ETextureType::e2D
			, uint32_t layers = 1
			, uint32_t mipLevels = 1
			, EMultiSampleCount samples = EMultiSampleCount::e1)
		{
			GPUTextureDescriptor desc{};
			desc.width = width;
			desc.height = height;
			desc.format = format;
			desc.textureType = textureType;
			desc.accessType = accessType;
			desc.layers = layers;
			desc.mipLevels = mipLevels;
			desc.samples = samples;
			return desc;
		}
	};

	struct GPUTextureView
	{
		ETextureAspect aspect;
		uint32_t baseMip;
		uint32_t mipCount;
		uint32_t baseLayer;
		uint32_t layerCount;

		auto operator<=>(const GPUTextureView&) const = default;

		constexpr static GPUTextureView Create(
			uint32_t baseMip = 0
			, uint32_t mipCount = 0
			, uint32_t baseLayer = 0
			, uint32_t layerCount = 0
			, ETextureAspect aspect = ETextureAspect::eDefault)
		{
			GPUTextureView desc{};
			desc.baseMip = baseMip;
			desc.mipCount = mipCount;
			desc.baseLayer = baseLayer;
			desc.layerCount = layerCount;
			desc.aspect = aspect;
			return desc;
		}

		constexpr static GPUTextureView CreateDefaultForRenderTarget(ETextureFormat format, uint32_t mip = 0u, uint32_t layer = 0u)
		{
			GPUTextureView desc{};
			desc.baseMip = mip;
			desc.mipCount = 1;
			desc.baseLayer = layer;
			desc.layerCount = 1;
			desc.aspect = ETextureAspect::eDefault;
			return desc;
		}

		constexpr static GPUTextureView CreateDefaultForSampling(ETextureFormat format)
		{
			bool hasDepth = FormatHasDepth(format);
			GPUTextureView desc{};
			desc.baseMip = 0;
			desc.mipCount = ~0;
			desc.baseLayer = 0;
			desc.layerCount = ~0;
			desc.aspect = hasDepth ? ETextureAspect::eDepth : ETextureAspect::eDefault;
			return desc;
		}

		constexpr void Sanitize(GPUTextureDescriptor const& textureDesc)
		{
			baseMip = castl::clamp(baseMip, 0u, textureDesc.mipLevels - 1);
			uint32_t remainCount = textureDesc.mipLevels - baseMip;
			mipCount = mipCount == 0u ? remainCount : (castl::min)(mipCount, remainCount);
			baseLayer = castl::clamp(baseLayer, 0u, textureDesc.layers - 1);
			remainCount = textureDesc.layers - baseLayer;
			layerCount = layerCount == 0u ? remainCount : (castl::min)(layerCount, remainCount);
		}
	};

	class GPUTexture
	{
	public:
		virtual GPUTextureDescriptor const& GetDescriptor() const = 0;
		virtual void SetName(castl::string const& name) = 0;
		virtual castl::string const& GetName() const = 0;
	};
}