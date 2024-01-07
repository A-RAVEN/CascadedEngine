#pragma once
#include <GeneralResources/header/IResource.h>
#include <GeneralResources/header/ResourceImporter.h>
#include <GeneralResources/header/ResourceManagingSystem.h>
#include <ExternalLib/zpp_bits/zpp_bits.h>
#include <RenderInterface/header/Common.h>

namespace resource_management
{
	class TextureResource : public IResource
	{
	public:
		friend zpp::bits::access;
		using serialize = zpp::bits::members<7>;
		virtual void Serialzie(std::vector<std::byte>& out) override;
		virtual void Deserialzie(std::vector<std::byte>& in) override;
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
	};
}