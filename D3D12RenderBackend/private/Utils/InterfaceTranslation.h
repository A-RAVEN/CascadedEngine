#pragma once
#include <Common.h>
#include <D3D12Includes.h>

namespace graphics_backend
{
	constexpr D3D12_RESOURCE_FLAGS ETextureAccessTypeToD3D12ResourceFlags(ETextureFormat format, ETextureAccessTypeFlags accessType)
	{
		D3D12_RESOURCE_FLAGS resultFlags = D3D12_RESOURCE_FLAG_NONE;
		bool anyShaderResource = false;
		for (std::underlying_type_t<ETextureAccessType> accessTypeId = 0
			; accessTypeId <= static_cast<std::underlying_type_t<ETextureAccessType>>(ETextureAccessType::eAccessType_Max)
			; ++accessTypeId)
		{
			ETextureAccessType typemask = static_cast<ETextureAccessType>(1 << accessTypeId);
			if (accessType & typemask)
			{
				switch (typemask)
				{
				case ETextureAccessType::eSampled:
					anyShaderResource = true;
					break;
				case ETextureAccessType::eRT:
					if (IsDepthStencilFormat(format))
					{
						resultFlags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
					}
					else
					{
						resultFlags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
					}
					break;
				case ETextureAccessType::eSubpassInput:
					anyShaderResource = true;
					break;
				case ETextureAccessType::eUnorderedAccess:
					anyShaderResource = true;
					resultFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
					break;
				case ETextureAccessType::eTransferDst:
					break;
				case ETextureAccessType::eTransferSrc:
					break;
				}
			}
		}
		if (!anyShaderResource)
		{
			if (resultFlags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
			{
				resultFlags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
			}
		}
		return resultFlags;
	}


	constexpr DXGI_FORMAT ETextureFormatToDXGIFotmat(ETextureFormat inFormat)
	{
		switch (inFormat)
		{
		case ETextureFormat::E_R8_UNORM:
			return DXGI_FORMAT_R8_UNORM;
		case ETextureFormat::E_R16_UNORM:
			return DXGI_FORMAT_R16_UNORM;
        case ETextureFormat::E_R16_SFLOAT:
			return DXGI_FORMAT_R16_FLOAT;
        case ETextureFormat::E_R8G8_UNORM:
			return DXGI_FORMAT_R8G8_UNORM;
        case ETextureFormat::E_B8G8R8A8_UNORM:
			return DXGI_FORMAT_B8G8R8A8_UNORM;
        case ETextureFormat::E_R16G16_SFLOAT:
			return DXGI_FORMAT_R16G16_FLOAT;
        case ETextureFormat::E_R8G8B8A8_UNORM:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
        case ETextureFormat::E_R16G16B16A16_UNORM:
			return DXGI_FORMAT_R16G16B16A16_UNORM;
        case ETextureFormat::E_R16G16B16A16_SFLOAT:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case ETextureFormat::E_R32_SFLOAT:
			return DXGI_FORMAT_R32_FLOAT;
        case ETextureFormat::E_R32G32B32A32_SFLOAT:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case ETextureFormat::E_D32_SFLOAT:
			return DXGI_FORMAT_D32_FLOAT;
        case ETextureFormat::E_D32_SFLOAT_S8_UINT:
			return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        case ETextureFormat::E_INVALID:
        default:
			return DXGI_FORMAT_UNKNOWN;
        }
	}

	constexpr UINT EMultiSampleCountToUint(EMultiSampleCount sampleCount)
	{
		switch (sampleCount)
		{
		case EMultiSampleCount::e1:
			return 1;
		case EMultiSampleCount::e2:
			return 2;
		case EMultiSampleCount::e4:
			return 4;
		case EMultiSampleCount::e8:
			return 8;
		case EMultiSampleCount::e16:
			return 16;
		case EMultiSampleCount::e32:
			return 32;
		case EMultiSampleCount::e64:
			return 64;
		default:
			CA_LOG_ERR("Unknown Sample Count!");
			return 1;
		}
	}

	constexpr D3D12_RESOURCE_DIMENSION ETextureTypeToResourceDimension(ETextureType textureType)
	{
		switch (textureType)
		{
		case ETextureType::e1D:
			return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
		case ETextureType::e2D:
			return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		case ETextureType::e3D:
			return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		case ETextureType::e2DArray:
			return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		case ETextureType::eCubeMap:
			return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		}
		CA_LOG_ERR("Unknown Texture Type!");
		return D3D12_RESOURCE_DIMENSION_UNKNOWN;
	}

	constexpr D3D12_BLEND EBlendFactorToD3D12Blend(EBlendFactor blendFactor)
    {
		switch (blendFactor)
		{
			case EBlendFactor::eZero:
				return D3D12_BLEND_ZERO;
			case EBlendFactor::eOne:
				return D3D12_BLEND_ONE;
			case EBlendFactor::eSrcAlpha:
				return D3D12_BLEND_SRC_ALPHA;
			case EBlendFactor::eOneMinusSrcAlpha:
				return D3D12_BLEND_INV_SRC_ALPHA;
			case EBlendFactor::eDstAlpha:
				return D3D12_BLEND_DEST_ALPHA;
			case EBlendFactor::eOneMinusDstAlpha:
				return D3D12_BLEND_INV_DEST_ALPHA;
			case EBlendFactor::eSrcColor:
				return D3D12_BLEND_SRC_COLOR;
			case EBlendFactor::eOneMinusSrcColor:
				return D3D12_BLEND_INV_SRC_COLOR;
			case EBlendFactor::eDstColor:
				return D3D12_BLEND_DEST_COLOR;
			case EBlendFactor::eOneMinusDstColor:
				return D3D12_BLEND_INV_DEST_COLOR;
			default:
				CA_LOG_ERR("Unknown Blend Factor!");
				return D3D12_BLEND_ZERO;
		}
    }

	constexpr D3D12_BLEND_OP EBlendOpToD3D12BlendOp(EBlendOp blendOp)
	{
		switch (blendOp)
		{
		case EBlendOp::eAdd:
			return D3D12_BLEND_OP_ADD;
		case EBlendOp::eSubtract:
			return D3D12_BLEND_OP_SUBTRACT;
		case EBlendOp::eReverseSubtract:
			return D3D12_BLEND_OP_REV_SUBTRACT;
		case EBlendOp::eMin:
			return D3D12_BLEND_OP_MIN;
		case EBlendOp::eMax:
			return D3D12_BLEND_OP_MAX;
		default:
			CA_LOG_ERR("Unknown Blend Op!");
			return D3D12_BLEND_OP_ADD;
		}
	}

	constexpr D3D12_COMPARISON_FUNC ECompareOpToD3D12Comparison(ECompareOp compareOp)
	{
		switch (compareOp)
		{
		case ECompareOp::eAlways:
			return D3D12_COMPARISON_FUNC_ALWAYS;
		case ECompareOp::eNever:
			return D3D12_COMPARISON_FUNC_NEVER;
		case ECompareOp::eLEqual:
			return D3D12_COMPARISON_FUNC_LESS_EQUAL;
		case ECompareOp::eGEqual:
			return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		case ECompareOp::eLess:
			return D3D12_COMPARISON_FUNC_LESS;
		case ECompareOp::eGreater:
			return D3D12_COMPARISON_FUNC_GREATER;
		case ECompareOp::eEqual:
			return D3D12_COMPARISON_FUNC_EQUAL;
		case ECompareOp::eUnequal:
			return D3D12_COMPARISON_FUNC_NOT_EQUAL;
		default:
			CA_LOG_ERR("Unknown Compare Op!");
			return D3D12_COMPARISON_FUNC_NONE;
		}
	}
}