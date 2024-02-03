#pragma once
#include "Common.h"
#include "GPUTexture.h"
#include "GPUGraphHandleBase.h"

namespace graphics_backend
{
	class WindowHandle;
	class ITextureHandleInternalInfo
	{
	public:
		virtual TIndex GetDescID() const = 0;
		virtual bool IsExternalTexture() const = 0;
		virtual WindowHandle* GetWindowsHandle() const = 0;
		virtual void ScheduleTextureData(uint64_t textureDataOffset, uint64_t dataSize, void* pData) = 0;
	};

	class TextureHandle : public GPUGraphHandleBase
	{
	public:
		TextureHandle(CRenderGraph* renderGraph, TIndex handleIndex) :
			GPUGraphHandleBase(renderGraph, handleIndex)
		{}
		inline TextureHandle& ScheduleTextureData(uint64_t textureDataOffset, uint64_t dataSize, void* pData);
		inline GPUTextureDescriptor const& GetTextureDesc() const;
	};
}