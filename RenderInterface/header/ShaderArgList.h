#pragma once
#include "ShaderResourceHandle.h"
#include "ShaderBindingBuilder.h"
#include "TextureSampler.h"
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

		inline ShaderArgList& SetValueArrayInternal(castl::string const& name, void const* pValue, uint32_t sizeInBytes)
		{
			auto& arrayData = m_NameToNumericArrayList[name];
			arrayData.resize(sizeInBytes);
			memcpy(arrayData.data(), pValue, sizeInBytes);
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
			return SetValueArrayInternal(name, pValue, sizeof(T) * count);
		}

		inline ShaderArgList& SetImage(castl::string const& name
			, ImageHandle const& imageHandle)
		{
			m_NameToImage[name] = { imageHandle };
			return *this;
		}

		inline ShaderArgList& SetBuffer(castl::string const& name
			, BufferHandle const& bufferHandle)
		{
			m_NameToBuffer[name] = { bufferHandle };
			return *this;
		}

		inline ShaderArgList& SetSampler(castl::string const& name
			, TextureSamplerDescriptor const& samplerDesc)
		{
			m_NameToSamplers[name] = samplerDesc;
			return *this;
		}

		inline ShaderArgList& SetSubArgList(castl::string const& name
			, castl::shared_ptr<ShaderArgList> const& subArgList)
		{
			m_NameToSubArgLists[name] = subArgList;
			return *this;
		}

		ShaderArgList const* FindSubArgList(castl::string const& name) const
		{
			auto found = m_NameToSubArgLists.find(name);
			if (found != m_NameToSubArgLists.end())
			{
				return found->second.get();
			}
			return nullptr;
		}
		void const* FindNumericDataPointer(castl::string const& name) const
		{
			auto found = m_NameToDataPosition.find(name);
			if (found != m_NameToDataPosition.end())
			{
				return &m_NumericDataList[found->second.offset];
			}
			auto foundArray = m_NameToNumericArrayList.find(name);
			if(foundArray!= m_NameToNumericArrayList.end())
			{
				return foundArray->second.data();
			}
			return nullptr;
		}

		castl::vector<ImageHandle> FindImageHandle(castl::string const& name) const
		{
			auto found = m_NameToImage.find(name);
			if (found != m_NameToImage.end())
			{
				return found->second;
			}
			return {};
		}
		
		castl::vector<BufferHandle> FindBufferHandle(castl::string const& name) const
		{
			auto found = m_NameToBuffer.find(name);
			if (found != m_NameToBuffer.end())
			{
				return found->second;
			}
			return {};
		}

		TextureSamplerDescriptor FindSampler(castl::string const& name) const
		{
			auto found = m_NameToSamplers.find(name);
			if (found != m_NameToSamplers.end())
			{
				return found->second;
			}
			return {};
		}

	private:
		castl::unordered_map<castl::string, castl::vector<uint8_t>> m_NameToNumericArrayList;
		castl::unordered_map<castl::string, castl::vector<ImageHandle>> m_NameToImage;
		castl::unordered_map<castl::string, castl::vector<BufferHandle>> m_NameToBuffer;
		castl::unordered_map<castl::string, castl::shared_ptr<ShaderArgList>> m_NameToSubArgLists;
		castl::unordered_map<castl::string, TextureSamplerDescriptor> m_NameToSamplers;
		castl::unordered_map<castl::string, NumericDataPos> m_NameToDataPosition;
		castl::vector<uint8_t> m_NumericDataList;
	};
}