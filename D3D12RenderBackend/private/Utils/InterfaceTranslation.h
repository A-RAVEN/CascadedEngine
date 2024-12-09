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

}