#pragma once
#include <Common.h>
#include <D3D12Includes.h>
#include <GPUBuffer.h>
#include <GPUTexture.h>

namespace graphics_backend
{
	

	constexpr D3D12_BARRIER_ACCESS EBufferUsageTranslate(EBufferUsage inUsage)
	{
		switch (inUsage)
		{
		case EBufferUsage::eConstantBuffer:
			return D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
		case EBufferUsage::eStructuredBuffer:
			return D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
		case EBufferUsage::eUnorderedAccess:
			return D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
			break;
		case EBufferUsage::eVertexBuffer:
			return D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
		case EBufferUsage::eIndexBuffer:
			return D3D12_BARRIER_ACCESS_INDEX_BUFFER;
		case EBufferUsage::eDataDst:
			return D3D12_BARRIER_ACCESS_COPY_DEST;
		case EBufferUsage::eDataSrc:
			return D3D12_BARRIER_ACCESS_COPY_SOURCE;
		default: return D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
		}
	}

	//constexpr D3D12_BARRIER_ACCESS EBufferUsagesToD3D12BarrierAccess(EBufferUsageFlags usageFlags)
	//{
	//	D3D12_BARRIER_ACCESS result;
	//	for (uint32_t i = 0
	//		; i <= static_cast<uint32_t>(EBufferUsage::eMaxBit)
	//		; ++i)
	//	{
	//		EBufferUsage itrUsage = static_cast<EBufferUsage>(1 << i);
	//		if (usageFlags & itrUsage)
	//		{
	//			result |= EBufferUsageTranslate(itrUsage);
	//		}
	//	}
	//	return result;
	//}

	constexpr D3D12_BARRIER_LAYOUT ETextureAccessTypeToD3D12BarrierLayout(ETextureFormat format, ETextureAccessType accessType)
	{
		switch (accessType)
		{
		case ETextureAccessType::eSampled:
		case ETextureAccessType::eSubpassInput:
			return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
			break;
		case ETextureAccessType::eRT:
			if (IsDepthStencilFormat(format))
			{
				return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
			}
			else
			{
				return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
			}
			break;
		case ETextureAccessType::eUnorderedAccess:
			return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
			break;
		case ETextureAccessType::eTransferDst:
			return D3D12_BARRIER_LAYOUT_COPY_DEST;
			break;
		case ETextureAccessType::eTransferSrc:
			return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
			break;
		}
	}

	constexpr D3D12_BARRIER_ACCESS ETextureAccessTypeToD3D12BarrierAccess(ETextureFormat format, ETextureAccessTypeFlags accessType)
	{
		D3D12_BARRIER_ACCESS resultAccess = D3D12_BARRIER_ACCESS_COMMON;
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
				case ETextureAccessType::eSubpassInput:
					resultAccess |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
					break;
				case ETextureAccessType::eRT:
					if (IsDepthStencilFormat(format))
					{
						resultAccess |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
					}
					else
					{
						resultAccess |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
					}
					break;
				case ETextureAccessType::eUnorderedAccess:
					resultAccess |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
					break;
				case ETextureAccessType::eTransferDst:
					resultAccess |= D3D12_BARRIER_ACCESS_COPY_DEST;
					break;
				case ETextureAccessType::eTransferSrc:
					resultAccess |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
					break;
				}
			}
		}
		return resultAccess;
	}

	constexpr D3D12_RESOURCE_FLAGS EBufferUsageFlagsToD3D12ResourceFlags(EBufferUsageFlags usageFlags)
	{
		D3D12_RESOURCE_FLAGS resultFlags = D3D12_RESOURCE_FLAG_NONE;
		for (std::underlying_type_t<EBufferUsage> accessTypeId = 0
			; accessTypeId <= static_cast<std::underlying_type_t<EBufferUsage>>(EBufferUsage::eMaxBit)
			; ++accessTypeId)
		{
			EBufferUsage typemask = static_cast<EBufferUsage>(1 << accessTypeId);
			if (usageFlags & typemask)
			{
				switch (typemask)
				{
				case EBufferUsage::eConstantBuffer:
				case EBufferUsage::eStructuredBuffer:
					resultFlags |= D3D12_RESOURCE_FLAGS::D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
					break;
				case EBufferUsage::eUnorderedAccess:
					resultFlags |= D3D12_RESOURCE_FLAGS::D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
					break;
				case EBufferUsage::eVertexBuffer:
				case EBufferUsage::eIndexBuffer:
				case EBufferUsage::eDataSrc:
				case EBufferUsage::eDataDst:
					break;
				default:
					CA_LOG_ERR_BREAK("Unknown Buffer Usage {}", typemask);
					break;
				}
			}
		}
		return resultFlags;
	}

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

	constexpr D3D12_SRV_DIMENSION ETextureTypeToSRVDimension(ETextureType textureType)
	{
		switch (textureType)
		{
		case ETextureType::e1D:
			return D3D12_SRV_DIMENSION_TEXTURE1D;
		case ETextureType::e2D:
			return D3D12_SRV_DIMENSION_TEXTURE2D;
		case ETextureType::e3D:
			return D3D12_SRV_DIMENSION_TEXTURE3D;
		case ETextureType::e2DArray:
			return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		case ETextureType::eCubeMap:
			return D3D12_SRV_DIMENSION_TEXTURECUBE;
		}
		CA_LOG_ERR_BREAK("Unknown Texture Type!");
		return D3D12_SRV_DIMENSION_UNKNOWN;
	}

	constexpr D3D12_UAV_DIMENSION ETextureTypeToUAVDimension(ETextureType textureType)
	{
		switch (textureType)
		{
		case ETextureType::e1D:
			return D3D12_UAV_DIMENSION_TEXTURE1D;
		case ETextureType::e2D:
			return D3D12_UAV_DIMENSION_TEXTURE2D;
		case ETextureType::e3D:
			return D3D12_UAV_DIMENSION_TEXTURE3D;
		case ETextureType::e2DArray:
			return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		case ETextureType::eCubeMap:
			return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		}
		CA_LOG_ERR_BREAK("Unknown Texture Type!");
		return D3D12_UAV_DIMENSION_UNKNOWN;
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
			return D3D12_COMPARISON_FUNC_ALWAYS;
		}
	}

	constexpr D3D12_CULL_MODE ECullModeToD3D12CullMode(ECullMode cullMode)
	{
		switch (cullMode)
		{
		case ECullMode::eNone:
			return D3D12_CULL_MODE_NONE;
		case ECullMode::eBack:
			return D3D12_CULL_MODE_BACK;
		case ECullMode::eFront:
			return D3D12_CULL_MODE_FRONT;
		default:
			CA_LOG_ERR("Unknown Cull Mode!");
			return D3D12_CULL_MODE_NONE;
		}
	}

	constexpr bool EFrontFaceIsClockWise(EFrontFace frontFace)
	{
		return frontFace == EFrontFace::eClockWise;
	}

	constexpr D3D12_PRIMITIVE_TOPOLOGY_TYPE ETopologyToD3D12TopologyType(ETopology topology)
	{
		switch (topology)
		{
		case ETopology::ePointList:
			return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		case ETopology::eLineList:
		case ETopology::eLineStrip:
			return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		case ETopology::eTriangleList:
		case ETopology::eTriangleStrip:
			return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		default:
			CA_LOG_ERR("Unknown Topology!");
			return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
		}
	}

	constexpr DXGI_FORMAT VertexInputFormatToDXGIFormat(VertexInputFormat vertexInputFormat)
	{
		switch (vertexInputFormat)
		{
		case VertexInputFormat::eR32_SFloat:
			return DXGI_FORMAT_R32_FLOAT;
		case VertexInputFormat::eR32G32_SFloat:
			return DXGI_FORMAT_R32G32_FLOAT;
		case VertexInputFormat::eR32G32B32_SFloat:
			return DXGI_FORMAT_R32G32B32_FLOAT;
		case VertexInputFormat::eR32G32B32A32_SFloat:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case VertexInputFormat::eR8G8B8A8_UNorm:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case VertexInputFormat::eR32_UInt:
			return DXGI_FORMAT_R32_UINT;
		case VertexInputFormat::eR32_SInt:
			return DXGI_FORMAT_R32_SINT;
		default:
			CA_LOG_ERR("Unknown Vertex Input Format!");
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	constexpr D3D12_RESOURCE_DESC GetResourceDescFromGPUBufferDescriptor(GPUBufferDescriptor const& inDescriptor)
	{
		return CD3DX12_RESOURCE_DESC::Buffer(inDescriptor.count * inDescriptor.stride, EBufferUsageFlagsToD3D12ResourceFlags(inDescriptor.usageFlags));
	}

	constexpr D3D12_RESOURCE_DESC GetResourceDescFromTextureDescriptor(GPUTextureDescriptor const& inDescriptor)
	{
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Alignment = 0;
		resourceDesc.Dimension = ETextureTypeToResourceDimension(inDescriptor.textureType);
		resourceDesc.Format = ETextureFormatToDXGIFotmat(inDescriptor.format);
		resourceDesc.Width = inDescriptor.width;
		resourceDesc.Height = inDescriptor.height;
		resourceDesc.DepthOrArraySize = inDescriptor.layers;
		resourceDesc.MipLevels = inDescriptor.mipLevels;
		resourceDesc.Flags = ETextureAccessTypeToD3D12ResourceFlags(inDescriptor.format, inDescriptor.accessType);
		resourceDesc.SampleDesc.Count = EMultiSampleCountToUint(inDescriptor.samples);
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		return resourceDesc;
	}

	constexpr D3D12_UNORDERED_ACCESS_VIEW_DESC GetUAVDescFromGPUTextureDescriptor(
		GPUTextureDescriptor const& inDescriptor
		, GPUTextureView textureView)
	{
		textureView.Sanitize(inDescriptor);
		D3D12_UNORDERED_ACCESS_VIEW_DESC result{};
		result.Format = ETextureFormatToDXGIFotmat(inDescriptor.format);
		result.ViewDimension = ETextureTypeToUAVDimension(inDescriptor.textureType);
		switch (inDescriptor.textureType)
		{
		case ETextureType::e1D:
		{
			result.Texture1D.MipSlice = textureView.baseMip;
		}
		break;
		case ETextureType::e2D:
			result.Texture2D.PlaneSlice = 0;
			result.Texture2D.MipSlice = textureView.baseMip;
			break;
		case ETextureType::e3D:
			result.Texture3D.MipSlice = textureView.baseMip;
			result.Texture3D.FirstWSlice = 0;
			break;
		case ETextureType::eCubeMap:
		case ETextureType::e2DArray:
			result.Texture2D.PlaneSlice = 0;
			result.Texture2DArray.MipSlice = textureView.baseMip;
			result.Texture2DArray.FirstArraySlice = textureView.baseLayer;
			result.Texture2DArray.ArraySize = textureView.layerCount;
			break;
		}
		return result;
	}

	constexpr D3D12_SHADER_RESOURCE_VIEW_DESC GetSRVDescFromGPUTextureDescriptor(
		GPUTextureDescriptor const& inDescriptor
		, GPUTextureView textureView)
	{
		textureView.Sanitize(inDescriptor);
		D3D12_SHADER_RESOURCE_VIEW_DESC result{};
		result.Format = ETextureFormatToDXGIFotmat(inDescriptor.format);
		result.ViewDimension = ETextureTypeToSRVDimension(inDescriptor.textureType);
		switch (inDescriptor.textureType)
		{
		case ETextureType::e1D:
		{
			result.Texture1D.MipLevels = textureView.mipCount;
			result.Texture1D.MostDetailedMip = textureView.baseMip;
			result.Texture1D.ResourceMinLODClamp = 0;
		}
			break;
		case ETextureType::e2D:
			result.Texture2D.PlaneSlice = 0;
			result.Texture2D.MipLevels = textureView.mipCount;
			result.Texture2D.MostDetailedMip = textureView.baseMip;
			result.Texture2D.ResourceMinLODClamp = 0;
			break;
		case ETextureType::e3D:
			result.Texture3D.MipLevels = textureView.mipCount;
			result.Texture3D.MostDetailedMip = textureView.baseMip;
			result.Texture3D.ResourceMinLODClamp = 0;
			break;
		case ETextureType::e2DArray:
			result.Texture2D.PlaneSlice = 0;
			result.Texture2DArray.MipLevels = textureView.mipCount;
			result.Texture2DArray.MostDetailedMip = textureView.baseMip;
			result.Texture2DArray.ResourceMinLODClamp = 0;
			result.Texture2DArray.FirstArraySlice = textureView.baseLayer;
			result.Texture2DArray.ArraySize = textureView.layerCount;
			break;
		case ETextureType::eCubeMap:
			result.TextureCube.MipLevels = textureView.mipCount;
			result.TextureCube.MostDetailedMip = textureView.baseMip;
			result.TextureCube.ResourceMinLODClamp = 0;
			break;
		}
		return result;
	}

	 
	constexpr D3D12_SHADER_RESOURCE_VIEW_DESC GetSRVDescFromGPUBufferDescriptor(GPUBufferDescriptor const& inDescriptor)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC result{};
		result.Format = DXGI_FORMAT_UNKNOWN;
		result.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		result.Buffer.FirstElement = 0;
		result.Buffer.NumElements = inDescriptor.count;
		result.Buffer.StructureByteStride = inDescriptor.stride;
		result.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		return result;
	}

	constexpr D3D12_UNORDERED_ACCESS_VIEW_DESC GetUAVDescFromGPUBufferDescriptor(GPUBufferDescriptor const& inDescriptor)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC result{};
		result.Format = DXGI_FORMAT_UNKNOWN;
		result.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		result.Buffer.CounterOffsetInBytes = 0;
		result.Buffer.FirstElement = 0;
		result.Buffer.NumElements = inDescriptor.count;
		result.Buffer.StructureByteStride = inDescriptor.stride;
		result.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
		return result;
	}

	constexpr D3D12_CONSTANT_BUFFER_VIEW_DESC GetCBVDescFromGPUBufferDescriptor(D3D12_GPU_VIRTUAL_ADDRESS address,
		GPUBufferDescriptor const& inDescriptor)
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC result{};
		result.BufferLocation = address;
		result.SizeInBytes = inDescriptor.count * inDescriptor.stride;
		return result;
	}
}