#pragma once
#include "Common.h"
#include "GPUTexture.h"

namespace graphics_backend
{
	class WindowHandle;
	class TextureHandle
	{
	public:
		TextureHandle(GPUTextureDescriptor const descriptor, TIndex handleIndex) :
			m_Descriptor(descriptor)
			, m_HandleIndex(handleIndex)
		{
		}

		GPUTextureDescriptor const& GetDescriptor() const {
			return m_Descriptor;
		}

		TIndex GetHandleIndex() const { return m_HandleIndex; }
	private:
		GPUTextureDescriptor m_Descriptor;
		TIndex m_HandleIndex;
	};


	struct TextureHandleInternalInfo
	{
	public:
		TIndex m_DescriptorIndex = INVALID_INDEX;
		std::shared_ptr<WindowHandle> p_WindowsHandle = nullptr;
		bool IsExternalTexture() const { return p_WindowsHandle != nullptr; }
	};
}