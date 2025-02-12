#pragma once
#include "ShaderResourceHandle.h"
#include "ShaderBindingBuilder.h"
#include "TextureSampler.h"
#include <CASTL/CAVector.h>
#include <CASTL/CAUnorderedMap.h>
//#include <CASTL/CAPair.h>

namespace graphics_backend
{
	//Interface for setting shader arguments
	class ShaderStruct
	{
	public:
		virtual void SetValueInternal(cacore::NameHash const& name
			, void const* pValue
			, uint32_t sizeInBytes
			, uint32_t elementIndex) = 0;
		
		virtual void SetImage(cacore::NameHash const& name
			, ImageHandle const& imageHandle, GPUTextureView const& view
			, uint32_t elementIndex) = 0;

		virtual void SetBuffer(cacore::NameHash const& name
			, BufferHandle const& bufferHandle
			, uint32_t elementIndex) = 0;

		virtual void SetSampler(cacore::NameHash const& name
			, TextureSamplerDescriptor const& samplerDesc
			, uint32_t elementIndex) = 0;

		virtual void SetStruct(cacore::NameHash const& name
			, castl::shared_ptr<ShaderStruct> const& subStruct
			, uint32_t elementIndex) = 0;
	};
}