#pragma once
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "WindowHandle.h"
#include <Hasher.h>

namespace graphics_backend
{
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
			: m_Name("")
			, m_ExternalManagedTexture(nullptr)
			, m_Backbuffer(nullptr)
			, m_Type(ImageType::Invalid)
			, m_UniqueID(0)
		{
		}
		ImageHandle(ImageHandle const& other)
			: m_Name(other.m_Name)
			, m_ExternalManagedTexture(other.m_ExternalManagedTexture)
			, m_Backbuffer(other.m_Backbuffer)
			, m_Type(other.m_Type)
			, m_UniqueID(other.m_UniqueID)
		{
		}
		ImageHandle(castl::string_view name, bool unique = false)
			: m_Name(name)
			, m_ExternalManagedTexture(nullptr)
			, m_Backbuffer(nullptr)
			, m_Type(ImageType::Internal)
			, m_UniqueID(unique ? -1 : 0)
		{
		}
		ImageHandle(castl::shared_ptr<GPUTexture> const& texture)
			: m_Name("")
			, m_ExternalManagedTexture(texture)
			, m_Backbuffer(nullptr)
			, m_Type(ImageType::External)
			, m_UniqueID(0)
		{
		}
		ImageHandle(castl::shared_ptr<WindowHandle> const& window)
			: m_Name("")
			, m_ExternalManagedTexture(nullptr)
			, m_Backbuffer(window)
			, m_Type(ImageType::Backbuffer)
			, m_UniqueID(0)
		{
		}
		ImageType GetType() const { return m_Type; }
		castl::string const& GetName() const { return m_Name; }
		castl::shared_ptr<GPUTexture> GetExternalManagedTexture() const { return m_ExternalManagedTexture; }
		castl::shared_ptr<WindowHandle> GetWindowHandle() const { return m_Backbuffer; }
		auto operator<=>(const ImageHandle&) const = default;
	private:
		castl::string m_Name;
		castl::shared_ptr<GPUTexture> m_ExternalManagedTexture;
		castl::shared_ptr<WindowHandle> m_Backbuffer;
		ImageType m_Type;
		int32_t m_UniqueID;

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
			: m_Name("")
			, m_ExternalManagedBuffer(nullptr)
			, m_Type(BufferType::Invalid)
			, m_UniqueID(0)
		{
		}
		BufferHandle(BufferHandle const& other)
			: m_Name(other.m_Name)
			, m_ExternalManagedBuffer(other.m_ExternalManagedBuffer)
			, m_Type(other.m_Type)
			, m_UniqueID(other.m_UniqueID)
		{
		}
		BufferHandle(castl::string_view name, bool unique = false)
			: m_Name(name)
			, m_ExternalManagedBuffer(nullptr)
			, m_Type(BufferType::Internal)
			, m_UniqueID(unique ? -1 : 0)
		{
		}
		BufferHandle(castl::shared_ptr<GPUBuffer> const& buffer)
			: m_Name("")
			, m_ExternalManagedBuffer(buffer)
			, m_Type(BufferType::External)
			, m_UniqueID(0)
		{
		}
		BufferType GetType() const { return m_Type; }
		castl::string const& GetName() const { return m_Name; }
		castl::shared_ptr<GPUBuffer> GetExternalManagedBuffer() const { return m_ExternalManagedBuffer; }
		auto operator<=>(const BufferHandle&) const = default;
	private:
		castl::string m_Name;
		castl::shared_ptr<GPUBuffer> m_ExternalManagedBuffer;
		BufferType m_Type;
		int32_t m_UniqueID;

		CA_PRIVATE_REFLECTION(BufferHandle);
	};
}

CA_REFLECTION(graphics_backend::ImageHandle, m_Name, m_ExternalManagedTexture, m_Backbuffer, m_Type, m_UniqueID);
CA_REFLECTION(graphics_backend::BufferHandle, m_Name, m_ExternalManagedBuffer, m_Type, m_UniqueID);
