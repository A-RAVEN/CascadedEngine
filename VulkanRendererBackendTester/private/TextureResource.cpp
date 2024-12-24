#include "TextureResource.h"
#include "SerializationLog.h"

namespace resource_management
{
	void TextureResource::Serialzie(castl::vector<uint8_t>& data)
	{
		cacore::serialize(data, *this);
	}
	//void TextureResource::Deserialzie(castl::vector<uint8_t>& data)
	//{
	//	cacore::deserializer<decltype(data)> deserializer(data);
	//	deserializer.deserialize(*this);
	//}
	void TextureResource::Deserialzie(ca_io::IOBatch* data)
	{
		cacore::batch_deserializer<ca_io::IOBatch> deserializer(data);
		deserializer.deserialize(*this);
		deserializer.finalize();
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