#include "Window_Impl.h"

namespace cawindow
{
	void WindowImpl::Initialize(castl::string const& windowName, int initialWidth, int initialHeight, bool visible, bool focused, bool decorate, bool floating)
	{

	}
	void WindowImpl::CloseWindow()
	{
	}
	void WindowImpl::ShowWindow()
	{
	}
	void WindowImpl::SetWindowPos(int inX, int inY)
	{
	}
	void WindowImpl::GetWindowPos(int& outX, int& outY) const
	{
	}
	void WindowImpl::SetWindowSize(int width, int height)
	{
	}
	void WindowImpl::GetWindowSize(int& outX, int& outY) const
	{
	}
	void WindowImpl::Focus()
	{
	}
	bool WindowImpl::GetWindowFocus() const
	{
		return false;
	}
	bool WindowImpl::GetWindowMinimized() const
	{
		return false;
	}
	void WindowImpl::SetWindowName(castl::string_view const& name)
	{
	}
	void WindowImpl::SetWindowAlpha(float alpha)
	{
	}
	float WindowImpl::GetDpiScale() const
	{
		return 0.0f;
	}
}