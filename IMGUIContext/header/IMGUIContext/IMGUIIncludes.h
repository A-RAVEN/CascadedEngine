#pragma once
#include <CASTL/CAString.h>

namespace imgui_display
{
	class IMGUIContext;
	class IIMGUIWindowBase
	{
	public:
		virtual void OnGUI(IMGUIContext*) = 0;
		virtual castl::string GetName() const = 0;
	};
}