#pragma once
#include <CASTL/CAString.h>
#include "GPUTexture.h"

namespace graphics_backend
{
	struct uint2
	{
		uint32_t x;
		uint32_t y;
	};

	struct int2
	{
		int x;
		int y;
	};

	class WindowHandle
	{
	public:
		virtual castl::string GetName() const = 0;
		virtual uint2 GetSizeSafe() const = 0;
		virtual GPUTextureDescriptor const& GetBackbufferDescriptor() const = 0;
		virtual void RecreateContext() = 0;
		virtual bool GetKeyState(int keycode, int state) const = 0;
		virtual bool IsKeyDown(int keycode) const = 0;
		virtual bool IsKeyTriggered(int keycode) const = 0;
		virtual bool IsMouseDown(int mousecode) const = 0;
		virtual bool IsMouseUp(int mousecode) const = 0;
		virtual float GetMouseX() const = 0;
		virtual float GetMouseY() const = 0;

		virtual void CloseWindow() = 0;
		virtual void ShowWindow() = 0;
		virtual void SetWindowPos(int x, int y) = 0;
		virtual int2 GetWindowPos() const = 0;
		virtual void SetWindowSize(uint32_t width, uint32_t height) = 0;
		virtual uint2 GetWindowSize() const = 0;
		virtual void Focus() = 0;
		virtual bool GetWindowFocus() const = 0;
		virtual bool GetWindowMinimized() const = 0;
		virtual void SetWindowName(castl::string_view const& name) = 0;
		virtual void SetWindowAlpha(float alpha) = 0;
		virtual float GetDpiScale() const = 0;
	};
}