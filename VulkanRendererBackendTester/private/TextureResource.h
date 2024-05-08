#pragma once
#include <Platform.h>
#include <CAResource/IResource.h>
#include <CAResource/ResourceImporter.h>
#include <CAResource/ResourceManagingSystem.h>
#include <Common.h>
#include <Serialization.h>
#include <Hasher.h>

namespace resource_management
{
	class TextureResource : public IResource
	{
	public:
		virtual void Serialzie(castl::vector<uint8_t>& out) override;
		virtual void Deserialzie(castl::vector<uint8_t>& in) override;
		void SetData(void* data, uint64_t size);
		void SetMetaData(uint32_t width, uint32_t height, uint32_t slices, uint32_t mipLevels, ETextureFormat format, ETextureType type);
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		uint32_t GetSlices() const { return m_Slices; }
		uint32_t GetMipLevels() const { return m_MipLevels; }
		void const* GetData() const { return m_Bytes.data(); }
		uint64_t GetDataSize() const { return m_Bytes.size(); }
		ETextureFormat GetFormat() const { return m_Format; }
	private:
		std::vector<uint8_t> m_Bytes;
		ETextureFormat m_Format;
		ETextureType m_Type;
		uint32_t m_Width;
		uint32_t m_Height;
		uint32_t m_Slices;
		uint32_t m_MipLevels;

		CA_PRIVATE_REFLECTION(TextureResource);
	};

	CA_REFLECTION(TextureResource
		, m_Bytes
		, m_Format
		, m_Type
		, m_Width
		, m_Height
		, m_Slices
		, m_MipLevels);
}