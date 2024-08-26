#pragma once
#include <CASTL/CAString.h>
//#include <uhash.h>
#include "Common.h"

namespace graphics_backend
{
	struct GPUBufferDescriptor
	{
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
		virtual GPUBufferDescriptor const& GetDescriptor() const = 0;
		virtual void SetName(castl::string const& name) = 0;
		virtual castl::string const& GetName() const = 0;
	};
}