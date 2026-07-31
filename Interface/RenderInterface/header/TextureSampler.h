#pragma once
#include <Hasher.h>

namespace graphics_backend
{
	enum class ETextureSamplerFilterMode
	{
		eNearest,
		eLinear,
	};

	enum class ETextureSamplerAddressMode
	{
		eRepeat,
		eMirroredRepeat,
		eClampToEdge,
		eClampToBorder,
	};

	enum class ETextureSamplerBorderColor
	{
		eTransparentBlack,
		eOpaqueBlack,
		eOpaqueWhite,
	};

	struct TextureSamplerDescriptor
	{
	public:
		ETextureSamplerFilterMode magFilterMode;
		ETextureSamplerFilterMode minFilterMode;
		ETextureSamplerFilterMode mipmapFilterMode;
		ETextureSamplerAddressMode addressModeU;
		ETextureSamplerAddressMode addressModeV;
		ETextureSamplerAddressMode addressModeW;
		ETextureSamplerBorderColor boarderColor;
		bool integerFormat;

		static TextureSamplerDescriptor Create(
			ETextureSamplerFilterMode filterMode = ETextureSamplerFilterMode::eLinear
			, ETextureSamplerAddressMode addressMode = ETextureSamplerAddressMode::eRepeat
			, ETextureSamplerBorderColor boarderColor = ETextureSamplerBorderColor::eTransparentBlack
			, bool integerFormat = false)
		{
			TextureSamplerDescriptor desc;
			desc.magFilterMode = filterMode;
			desc.minFilterMode = filterMode;
			desc.mipmapFilterMode = filterMode;
			desc.addressModeU = addressMode;
			desc.addressModeV = addressMode;
			desc.addressModeW = addressMode;
			desc.boarderColor = boarderColor;
			desc.integerFormat = integerFormat;
			return desc;
		}
		static TextureSamplerDescriptor const& LinearClamp()
		{
			const static TextureSamplerDescriptor sampler = Create(ETextureSamplerFilterMode::eLinear
				, ETextureSamplerAddressMode::eClampToEdge);
			return sampler;
		}
		static TextureSamplerDescriptor const& LinearRepeat()
		{
			const static TextureSamplerDescriptor sampler = Create(ETextureSamplerFilterMode::eLinear
				, ETextureSamplerAddressMode::eRepeat);
			return sampler;
		}
		static TextureSamplerDescriptor const& PointClamp()
		{
			const static TextureSamplerDescriptor sampler = Create(ETextureSamplerFilterMode::eNearest
				, ETextureSamplerAddressMode::eClampToEdge);
			return sampler;
		}
		static TextureSamplerDescriptor const& PointRepeat()
		{
			const static TextureSamplerDescriptor sampler = Create(ETextureSamplerFilterMode::eNearest
				, ETextureSamplerAddressMode::eRepeat);
			return sampler;
		}
		auto operator<=>(const TextureSamplerDescriptor&) const = default;
	};

	using TextureSamplerDescriptorObj = cacore::HashObj<TextureSamplerDescriptor>;
}