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

	class WindowHandle
	{
	public:
		virtual castl::string GetName() const = 0;
		virtual uint2 GetSizeSafe() const = 0;
		virtual GPUTextureDescriptor const& GetBackbufferDescriptor() const = 0;
		virtual void RecreateContext() = 0;
		virtual bool IsKeyDown(int keycode) const = 0;
		virtual bool IsKeyTriggered(int keycode) const = 0;
		virtual bool IsMouseDown(int mousecode) const = 0;
		virtual bool IsMouseUp(int mousecode) const = 0;
		virtual float GetMouseX() const = 0;
		virtual float GetMouseY() const = 0;

		virtual void CloseWindow() = 0;
		virtual void ShowWindow() = 0;
		virtual void SetWindowPos(uint32_t x, uint32_t y) = 0;
		virtual uint2 GetWindowPos() const = 0;
		virtual void SetWindowSize(uint32_t width, uint32_t height) = 0;
		virtual uint2 GetWindowSize() const = 0;
		virtual void Focus() = 0;
		virtual bool GetWindowFocus() const = 0;
		virtual bool GetWindowMinimized() const = 0;
		virtual void SetWindowName(castl::string_view const& name) = 0;
		virtual void SetWindowAlpha(float alpha) = 0;
		virtual float GetDpiScale() const = 0;

		virtual void SetWindowFocusCallback(castl::function<void(WindowHandle*, bool)> callback);
		virtual void SetCursorEnterCallback(castl::function<void(WindowHandle*)> callback);
		virtual void SetCursorPosCallback(castl::function<void(WindowHandle*, float, float)> callback);
		virtual void SetMouseButtonCallback(castl::function<void(WindowHandle*, int, int, int)> callback);
		virtual void SetScrollCallback(castl::function<void(WindowHandle*, float, float)> callback);
		virtual void SetKeyCallback(castl::function<void(WindowHandle*, int, int, int, int)> callback);
		virtual void SetCharCallback(castl::function<void(WindowHandle*, uint32_t)> callback);
		virtual void SetWindowCloseCallback(castl::function<void(WindowHandle*)> callback);
		virtual void SetWindowPosCallback(castl::function<void(WindowHandle*, float, float)> callback);
		virtual void SetWindowSizeCallback(castl::function<void(WindowHandle*, float,float)> callback);
	};
}