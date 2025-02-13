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
	protected:
		virtual void SetValueInternal(cacore::NameHash const& name
			, void const* pValue
			, uint32_t sizeInBytes
			, uint32_t elementIndex) = 0;
		
		virtual void SetImageInternal(cacore::NameHash const& name
			, ImageHandle const& imageHandle, GPUTextureView const& view
			, uint32_t elementIndex) = 0;

		virtual void SetBufferInternal(cacore::NameHash const& name
			, BufferHandle const& bufferHandle
			, uint32_t elementIndex) = 0;

		virtual void SetSamplerInternal(cacore::NameHash const& name
			, TextureSamplerDescriptor const& samplerDesc
			, uint32_t elementIndex) = 0;

		virtual void SetStructInternal(cacore::NameHash const& name
			, castl::shared_ptr<ShaderStruct> const& subStruct
			, uint32_t elementIndex) = 0;

	public:
		template<typename T>
		ShaderStruct& SetValue(cacore::NameHash const& name, T const& value, uint32_t elementIndex = 0)
		{
			SetValueInternal(name, &value, sizeof(T), elementIndex);
			return *this;
		}

		ShaderStruct& SetImage(cacore::NameHash const& name
			, ImageHandle const& imageHandle, GPUTextureView const& view
			, uint32_t elementIndex)
		{
			SetImageInternal(name, imageHandle, view, elementIndex);
			return *this;
		}

		ShaderStruct& SetBuffer(cacore::NameHash const& name
			, BufferHandle const& bufferHandle
			, uint32_t elementIndex)
		{
			SetBufferInternal(name, bufferHandle, elementIndex);
			return *this;
		}

		ShaderStruct& SetSampler(cacore::NameHash const& name
			, TextureSamplerDescriptor const& samplerDesc
			, uint32_t elementIndex)
		{
			SetSamplerInternal(name, samplerDesc, elementIndex);
			return *this;
		}

		ShaderStruct& SetStruct(cacore::NameHash const& name
			, castl::shared_ptr<ShaderStruct> const& subStruct
			, uint32_t elementIndex)
		{
			SetStructInternal(name, subStruct, elementIndex);
			return *this;
		}
	};
}