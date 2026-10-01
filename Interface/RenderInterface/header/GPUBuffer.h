#pragma once
#include <CASTL/CAString.h>
#include "Common.h"
// The CodeGen marker macro must be a REAL macro invocation here: the code generator decides which
// types are annotated from the preprocessor, not by scanning the source text.
#include "CodeGenMeta.h"

namespace graphics_backend
{
	// Reflects this type into the generated metadata (reflection / serialization / hash). The marker
	// binds to the FIRST file- or namespace-scope type that follows it.
	CA_REFLECT(serializable)
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