#pragma once
#include <uhash.h>
#include "Common.h"

namespace graphics_backend
{
	struct GPUTextureDescriptor
	{
	public:
		ETextureFormat format = ETextureFormat::E_R8G8B8A8_UNORM;
		ETextureType textureType = ETextureType::e2D;
		ETextureAccessTypeFlags accessType = ETextureAccessType::eSampled;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t layers = 1;
		uint32_t mipLevels = 1;
		EMultiSampleCount samples = EMultiSampleCount::e1;
		GPUTextureDescriptor() = default;
		GPUTextureDescriptor(uint32_t inWidth, uint32_t inHeight, ETextureFormat inFormat, ETextureAccessTypeFlags inAccessTypes) :
			width(inWidth)
			,height(inHeight)
			,format(inFormat)
			,accessType(inAccessTypes)
		{}

		bool operator==(GPUTextureDescriptor const& other) const
		{
			return hash_utils::memory_equal(*this, other);
		}
	};

	class GPUTextureAccessDescriptor
	{
	public:
		ETextureAspect aspect = ETextureAspect::eDefault;
		uint32_t baseMip = 0;
		uint32_t mipCount = 1;
		uint32_t baseLayer = 0;
		uint32_t layerCount = 1;
	};

	class GPUTexture
	{
	public:
		virtual void ScheduleTextureData(uint64_t textureDataOffset, uint64_t dataSize, void const* pData) = 0;
		virtual bool UploadingDone() const = 0;
		virtual GPUTextureDescriptor const& GetDescriptor() const = 0;
	protected:
	};
}

namespace hash_utils
{
	template<>
	struct is_contiguously_hashable<graphics_backend::GPUTextureDescriptor> : public std::true_type {};
}
