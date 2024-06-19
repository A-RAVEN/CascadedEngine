#pragma once
#include <CAWindow/WindowSystem.h>
#include "GLFWInclude.h"

namespace cawindow
{
	class WindowSystem;
	class WindowImpl : public IWindow
	{
		void Initialize(WindowSystem* windowSystem
			, castl::string const& windowName
			, int initialWidth
			, int initialHeight
			, bool visible
			, bool focused
			, bool decorate
			, bool floating);
		void Release();

		// 通过 IWindow 继承
		void CloseWindow() override;
		void ShowWindow() override;
		void SetWindowPos(int inX, int inY) override;
		void GetWindowPos(int& outX, int& outY) const override;
		void SetWindowSize(int width, int height) override;
		void GetWindowSize(int& outX, int& outY) const override;
		void Focus() override;
		bool GetWindowFocus() const override;
		bool GetWindowMinimized() const override;
		void SetWindowName(castl::string const& name) override;
		castl::string_view GetWindowName() const override;
		void SetWindowAlpha(float alpha) override;
		float GetDpiScale() const override;
		IWindowSystem* GetWindowSystem() override;
	private:
		friend class glfwContext;
		friend class WindowSystem;
		WindowSystem* m_WindowSystem;
		castl::string m_WindowName;
		GLFWwindow* m_Window = nullptr;

		// 通过 IWindow 继承
		void* GetNativeWindowHandle() override;
#if defined(_WIN32) || defined(_WIN64)
		HWND m_Win32Window;
#endif
	};
}