#pragma once
#include <CASTL/CAString.h>
#include "Common.h"

namespace graphics_backend
{
	struct GPUBufferDescriptor
	{
		uint64_t count;
		uint64_t stride;
		uint64_t SizeInByte() const
		{
			return count * stride;
		}
		auto operator<=>(const GPUBufferDescriptor&) const = default;

		static GPUBufferDescriptor Create(uint64_t count, uint64_t stride)
		{
			return GPUBufferDescriptor{ count, stride };
		}
	};

	class GPUBuffer
	{
	public:
		virtual GPUBufferDescriptor const& GetDescriptor() const = 0;
		virtual void SetName(castl::string const& name) = 0;
		virtual castl::string const& GetName() const = 0;
	};
}