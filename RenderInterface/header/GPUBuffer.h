#pragma once
#include <RenderInterface/header/Common.h>

namespace graphics_backend
{
	class GPUBufferDescriptor
	{
	public:
		EBufferUsageFlags usageFlags;
		uint64_t count;
		uint64_t stride;
		bool operator==(GPUBufferDescriptor const& other) const
		{
			return hash_utils::memory_equal(*this, other);
		}
	};

	class GPUBuffer
	{
	public:
		virtual void ScheduleBufferData(uint64_t bufferOffset, uint64_t dataSize, void const* pData) = 0;
		virtual void UploadAsync() = 0;
		virtual bool UploadingDone() const = 0;
	};
}

template<>
struct hash_utils::is_contiguously_hashable<graphics_backend::GPUBufferDescriptor> : public std::true_type {};