#pragma once
#include "ShaderResourceHandle.h"
#include "ShaderBindingBuilder.h"
#include <CASTL/CAVector.h>
#include <CASTL/CAUnorderedMap.h>

namespace graphics_backend
{
	class ShaderArgList
	{
	public:
		struct NumericDataPos
		{
			uint32_t offset;
			uint32_t size;
		};
		inline ShaderArgList& SetValueInternal(castl::string const& name, void const* pValue, uint32_t sizeInBytes)
		{
			auto found = m_NameToDataPosition.find(name);
			if (found == m_NameToDataPosition.end())
			{
				CA_ASSERT(found->second.size >= sizeInBytes, castl::string("shader parameter ") + name + "has a data size longer than first set");
			}
			else
			{
				uint32_t offset = m_NumericDataList.size();
				m_NumericDataList.resize(offset + sizeInBytes);
				found = m_NameToDataPosition.insert(castl::make_pair(name, NumericDataPos{ offset, sizeInBytes })).first;
			}
			memcpy(&m_NumericDataList[found->second.offset], pValue, castl::min(found->second.size, sizeInBytes));
			return *this;
		}
		template<typename T>
		ShaderArgList& SetValue(castl::string const& name, T const& value)
		{
			return SetValueInternal(name, &value, sizeof(T));
		}
		template<typename T>
		ShaderArgList& SetValueArray(castl::string const& name, T const* pValue, uint32_t count)
		{
			return SetValueInternal(name, pValue, sizeof(T) * count);
		}

		inline ShaderArgList& SetImage(castl::string const& name
			, ImageHandle const& imageHandle)
		{
			m_NameToImage[name] = imageHandle;
			return *this;
		}

		inline ShaderArgList& SetBuffer(castl::string const& name
			, BufferHandle const& bufferHandle)
		{
			m_NameToBuffer[name] = bufferHandle;
			return *this;
		}
	private:
		castl::unordered_map<castl::string, NumericDataPos> m_NameToDataPosition;
		castl::unordered_map<castl::string, ImageHandle> m_NameToImage;
		castl::unordered_map<castl::string, BufferHandle> m_NameToBuffer;
		castl::vector<uint8_t> m_NumericDataList;
	};
}