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
			desc.magFilterMode = ETextureSamplerFilterMode::eLinear;
			desc.minFilterMode = ETextureSamplerFilterMode::eLinear;
			desc.mipmapFilterMode = ETextureSamplerFilterMode::eLinear;
			desc.addressModeU = ETextureSamplerAddressMode::eClampToEdge;
			desc.addressModeV = ETextureSamplerAddressMode::eClampToEdge;
			desc.addressModeW = ETextureSamplerAddressMode::eClampToEdge;
			desc.boarderColor = ETextureSamplerBorderColor::eTransparentBlack;
			desc.integerFormat = false;
			return desc;
		}
		auto operator<=>(const TextureSamplerDescriptor&) const = default;
	};

	using TextureSamplerDescriptorObj = cacore::HashObj<TextureSamplerDescriptor>;
}