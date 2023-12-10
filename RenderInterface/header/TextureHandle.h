#pragma once
#include "Common.h"
#include "GPUTexture.h"

namespace graphics_backend
{
	class WindowHandle;
	class TextureHandle
	{
	public:
		TextureHandle(TIndex handleIndex) :
			m_HandleIndex(handleIndex)
		{
		}
		TIndex GetHandleIndex() const { return m_HandleIndex; }
	private:
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