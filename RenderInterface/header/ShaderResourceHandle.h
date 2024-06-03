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
		{
		}
		ImageHandle(ImageHandle const& other)
			: m_Name(other.m_Name)
			, m_ExternalManagedTexture(other.m_ExternalManagedTexture)
			, m_Backbuffer(other.m_Backbuffer)
			, m_Type(other.m_Type)
		{
		}
		ImageHandle(castl::string_view name)
			: m_Name(name)
			, m_ExternalManagedTexture(nullptr)
			, m_Backbuffer(nullptr)
			, m_Type(ImageType::Internal)
		{
		}
		ImageHandle(castl::shared_ptr<GPUTexture> const& texture)
			: m_Name("")
			, m_ExternalManagedTexture(texture)
			, m_Backbuffer(nullptr)
			, m_Type(ImageType::External)
		{
		}
		ImageHandle(castl::shared_ptr<WindowHandle> const& window)
			: m_Name("")
			, m_ExternalManagedTexture(nullptr)
			, m_Backbuffer(window)
			, m_Type(ImageType::Backbuffer)
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
		{
		}
		BufferHandle(castl::string_view name)
			: m_Name(name)
			, m_ExternalManagedBuffer(nullptr)
			, m_Type(BufferType::Internal)
		{
		}
		BufferHandle(castl::shared_ptr<GPUBuffer> const& buffer)
			: m_Name("")
			, m_ExternalManagedBuffer(buffer)
			, m_Type(BufferType::External)
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

		CA_PRIVATE_REFLECTION(BufferHandle);
	};
}

CA_REFLECTION(graphics_backend::ImageHandle, m_Name, m_ExternalManagedTexture, m_Backbuffer, m_Type);
CA_REFLECTION(graphics_backend::BufferHandle, m_Name, m_ExternalManagedBuffer, m_Type);