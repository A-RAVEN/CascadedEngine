#include "TextureResource.h"
#include "SerializationLog.h"

namespace resource_management
{
	void TextureResource::Serialzie(castl::vector<uint8_t>& data)
	{
		zpp::bits::out out(data);
		auto result = out(*this);
		if (failure(result)) {
			LogZPPError("serialize failed", result);
		}
	}
	void TextureResource::Deserialzie(castl::vector<uint8_t>& data)
	{
		zpp::bits::in in(data);
		auto result = in(*this);
		if (failure(result)) {
			LogZPPError("deserialize failed", result);
		}
	}
	void TextureResource::SetData(void* data, uint64_t size)
	{
		m_Bytes.resize(size);
		memcpy(m_Bytes.data(), data, size);
	}
	void TextureResource::SetMetaData(uint32_t width, uint32_t height, uint32_t slices, uint32_t mipLevels, ETextureFormat format, ETextureType type)
	{
		m_Width = width;
		m_Height = height;
		m_Slices = slices;
		m_MipLevels = mipLevels;
		m_Format = format;
		m_Type = type;
	}
}