#pragma once
#include <CASTL/CAString.h>
#include <uhash.h>
#include "Common.h"

namespace graphics_backend
{
	class GPUBufferDescriptor
	{
	public:
		EBufferUsageFlags usageFlags;
		uint64_t count;
		uint64_t stride;

		auto operator<=>(const GPUBufferDescriptor&) const = default;

		static GPUBufferDescriptor Create(EBufferUsageFlags usageFlags, uint64_t count, uint64_t stride)
		{
			return { usageFlags, count, stride };
		}
	};

	class GPUBuffer
	{
	public:
		virtual void ScheduleBufferData(uint64_t bufferOffset, uint64_t dataSize, void const* pData) = 0;
		virtual bool UploadingDone() const = 0;
		virtual void Name(castl::string const& name) = 0;
	};
}

template<>
struct hash_utils::is_contiguously_hashable<graphics_backend::GPUBufferDescriptor> : public castl::true_type {};