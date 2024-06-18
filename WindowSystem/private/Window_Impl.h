#pragma once
#include <CAWindow/WindowSystem.h>
#include <GLFW/glfw3.h>

namespace cawindow
{
	class WindowImpl : public IWindow
	{
		void Initialize(castl::string const& windowName
			, int initialWidth
			, int initialHeight
			, bool visible
			, bool focused
			, bool decorate
			, bool floating);

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
		void SetWindowName(castl::string_view const& name) override;
		void SetWindowAlpha(float alpha) override;
		float GetDpiScale() const override;
	private:
		friend class glfwContext;
		castl::string m_WindowName;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		int m_PosX = 0;
		int m_PosY = 0;
		GLFWwindow* m_Window = nullptr;
	};
}