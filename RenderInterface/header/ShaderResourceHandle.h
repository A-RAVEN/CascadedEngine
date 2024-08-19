#pragma once
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "WindowHandle.h"
#include <Hasher.h>

namespace graphics_backend
{
	struct ResourceHandleKeyData
	{
		castl::string name;
		uint32_t uniqueID;
		auto operator<=>(const ResourceHandleKeyData&) const = default;
		static ResourceHandleKeyData Default() { return { "", 0 }; }
		static ResourceHandleKeyData Create(castl::string const& name, uint32_t uniqueID) { return { name, uniqueID }; }
	};

	using ResourceHandleKey = cacore::HashObj<ResourceHandleKeyData, true>;

	class ImageHandle
	{
	public:
		enum class ImageType
		{
			Internal,
			External,
			Backbuffer,
			Invalid,
		};
		ImageHandle()
			: m_Key(ResourceHandleKeyData::Default())
			, m_ExternalManagedTexture(nullptr)
			, m_Backbuffer(nullptr)
			, m_Type(ImageType::Invalid)
		{
		}
		ImageHandle(ImageHandle const& other)
			: m_Key(other.m_Key)
			, m_ExternalManagedTexture(other.m_ExternalManagedTexture)
			, m_Backbuffer(other.m_Backbuffer)
			, m_Type(other.m_Type)
		{
		}
		ImageHandle(castl::string const& name, uint32_t uniqueID = 0)
			: m_Key(ResourceHandleKeyData::Create(name, uniqueID))
			, m_ExternalManagedTexture(nullptr)
			, m_Backbuffer(nullptr)
			, m_Type(ImageType::Internal)
		{
		}
		ImageHandle(castl::shared_ptr<GPUTexture> const& texture)
			: m_Key(ResourceHandleKeyData::Default())
			, m_ExternalManagedTexture(texture)
			, m_Backbuffer(nullptr)
			, m_Type(ImageType::External)
		{
		}
		ImageHandle(castl::shared_ptr<WindowHandle> const& window)
			: m_Key(ResourceHandleKeyData::Default())
			, m_ExternalManagedTexture(nullptr)
			, m_Backbuffer(window)
			, m_Type(ImageType::Backbuffer)
		{
		}
		bool IsValid() const { return m_Type != ImageType::Invalid; }
		ImageType GetType() const { return m_Type; }
		castl::string const& GetName() const { return m_Key.Get().name; }
		ResourceHandleKey const& GetKey() const { return m_Key; }
		castl::shared_ptr<GPUTexture> GetExternalManagedTexture() const { return m_ExternalManagedTexture; }
		castl::shared_ptr<WindowHandle> GetWindowHandle() const { return m_Backbuffer; }
		auto operator<=>(const ImageHandle&) const = default;
	private:
		ResourceHandleKey m_Key;
		castl::shared_ptr<GPUTexture> m_ExternalManagedTexture;
		castl::shared_ptr<WindowHandle> m_Backbuffer;
		ImageType m_Type;

		CA_PRIVATE_REFLECTION(ImageHandle);
	};

	class BufferHandle
	{
	public:
		enum class BufferType
		{
			Internal,
			External,
			Invalid,
		};
		BufferHandle()
			: m_Key(ResourceHandleKeyData::Default())
			, m_ExternalManagedBuffer(nullptr)
			, m_Type(BufferType::Invalid)
		{
		}
		BufferHandle(BufferHandle const& other)
			: m_Key(other.m_Key)
			, m_ExternalManagedBuffer(other.m_ExternalManagedBuffer)
			, m_Type(other.m_Type)
		{
		}
		BufferHandle(castl::string const& name, uint32_t uniqueID = 0)
			: m_Key(ResourceHandleKeyData::Create(name, uniqueID))
			, m_ExternalManagedBuffer(nullptr)
			, m_Type(BufferType::Internal)
		{
		}
		BufferHandle(castl::shared_ptr<GPUBuffer> const& buffer)
			: m_Key(ResourceHandleKeyData::Default())
			, m_ExternalManagedBuffer(buffer)
			, m_Type(BufferType::External)
		{
		}
		bool IsValid() const { return m_Type != BufferType::Invalid; }
		BufferType GetType() const { return m_Type; }
		castl::string const& GetName() const { return m_Key.Get().name; }
		ResourceHandleKey const& GetKey() const { return m_Key; }
		castl::shared_ptr<GPUBuffer> GetExternalManagedBuffer() const { return m_ExternalManagedBuffer; }
		auto operator<=>(const BufferHandle&) const = default;
	private:
		ResourceHandleKey m_Key;
		castl::shared_ptr<GPUBuffer> m_ExternalManagedBuffer;
		BufferType m_Type;

		CA_PRIVATE_REFLECTION(BufferHandle);
	};
}

CA_REFLECTION(graphics_backend::ImageHandle, m_Key, m_ExternalManagedTexture, m_Backbuffer, m_Type);
CA_REFLECTION(graphics_backend::BufferHandle, m_Key, m_ExternalManagedBuffer, m_Type);
