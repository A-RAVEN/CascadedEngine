#pragma once
#include <string>
#include "GPUTexture.h"

namespace graphics_backend
{
	struct uint2
	{
		uint32_t x;
		uint32_t y;
	};

	class WindowHandle
	{
	public:
		virtual std::string GetName() const = 0;
		virtual uint2 const& GetSizeSafe() const = 0;
		virtual GPUTextureDescriptor const& GetBackbufferDescriptor() const = 0;
		virtual void RecreateContext() = 0;
		virtual bool IsKeyDown(int keycode) const = 0;
		virtual bool IsKeyTriggered(int keycode) const = 0;
		virtual bool IsMouseDown(int mousecode) const = 0;
		virtual bool IsMouseUp(int mousecode) const = 0;
		virtual float GetMouseX() const = 0;
		virtual float GetMouseY() const = 0;
	};
}