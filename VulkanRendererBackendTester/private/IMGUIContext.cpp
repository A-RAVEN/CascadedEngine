#include <GPUTexture.h>
#include <imgui.h>
#include <glm/mat4x4.hpp>
#include "IMGUIContext.h"
#include "ShaderResource.h"
#include "KeyCodes.h"
#include <CASTL/CAString.h>
#include <CAWindow/WindowSystem.h>

namespace imgui_display
{
	using namespace graphics_backend;
	using namespace cawindow;
	using namespace resource_management;
	void InitImGUIPlatformFunctors();

	IMGUIContext* GetIMGUIContext()
	{
		ImGuiIO& io = ImGui::GetIO();
		return static_cast<IMGUIContext*>(io.BackendPlatformUserData);
	}

	static ImGuiKey ImGui_ImplGlfw_KeyToImGuiKey(int key)
	{
		switch (key)
		{
		case CA_KEY_TAB: return ImGuiKey_Tab;
		case CA_KEY_LEFT: return ImGuiKey_LeftArrow;
		case CA_KEY_RIGHT: return ImGuiKey_RightArrow;
		case CA_KEY_UP: return ImGuiKey_UpArrow;
		case CA_KEY_DOWN: return ImGuiKey_DownArrow;
		case CA_KEY_PAGE_UP: return ImGuiKey_PageUp;
		case CA_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
		case CA_KEY_HOME: return ImGuiKey_Home;
		case CA_KEY_END: return ImGuiKey_End;
		case CA_KEY_INSERT: return ImGuiKey_Insert;
		case CA_KEY_DELETE: return ImGuiKey_Delete;
		case CA_KEY_BACKSPACE: return ImGuiKey_Backspace;
		case CA_KEY_SPACE: return ImGuiKey_Space;
		case CA_KEY_ENTER: return ImGuiKey_Enter;
		case CA_KEY_ESCAPE: return ImGuiKey_Escape;
		case CA_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
		case CA_KEY_COMMA: return ImGuiKey_Comma;
		case CA_KEY_MINUS: return ImGuiKey_Minus;
		case CA_KEY_PERIOD: return ImGuiKey_Period;
		case CA_KEY_SLASH: return ImGuiKey_Slash;
		case CA_KEY_SEMICOLON: return ImGuiKey_Semicolon;
		case CA_KEY_EQUAL: return ImGuiKey_Equal;
		case CA_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
		case CA_KEY_BACKSLASH: return ImGuiKey_Backslash;
		case CA_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
		case CA_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
		case CA_KEY_CAPS_LOCK: return ImGuiKey_CapsLock;
		case CA_KEY_SCROLL_LOCK: return ImGuiKey_ScrollLock;
		case CA_KEY_NUM_LOCK: return ImGuiKey_NumLock;
		case CA_KEY_PRINT_SCREEN: return ImGuiKey_PrintScreen;
		case CA_KEY_PAUSE: return ImGuiKey_Pause;
		case CA_KEY_KP_0: return ImGuiKey_Keypad0;
		case CA_KEY_KP_1: return ImGuiKey_Keypad1;
		case CA_KEY_KP_2: return ImGuiKey_Keypad2;
		case CA_KEY_KP_3: return ImGuiKey_Keypad3;
		case CA_KEY_KP_4: return ImGuiKey_Keypad4;
		case CA_KEY_KP_5: return ImGuiKey_Keypad5;
		case CA_KEY_KP_6: return ImGuiKey_Keypad6;
		case CA_KEY_KP_7: return ImGuiKey_Keypad7;
		case CA_KEY_KP_8: return ImGuiKey_Keypad8;
		case CA_KEY_KP_9: return ImGuiKey_Keypad9;
		case CA_KEY_KP_DECIMAL: return ImGuiKey_KeypadDecimal;
		case CA_KEY_KP_DIVIDE: return ImGuiKey_KeypadDivide;
		case CA_KEY_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
		case CA_KEY_KP_SUBTRACT: return ImGuiKey_KeypadSubtract;
		case CA_KEY_KP_ADD: return ImGuiKey_KeypadAdd;
		case CA_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
		case CA_KEY_KP_EQUAL: return ImGuiKey_KeypadEqual;
		case CA_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
		case CA_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
		case CA_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
		case CA_KEY_LEFT_SUPER: return ImGuiKey_LeftSuper;
		case CA_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
		case CA_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
		case CA_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
		case CA_KEY_RIGHT_SUPER: return ImGuiKey_RightSuper;
		case CA_KEY_MENU: return ImGuiKey_Menu;
		case CA_KEY_0: return ImGuiKey_0;
		case CA_KEY_1: return ImGuiKey_1;
		case CA_KEY_2: return ImGuiKey_2;
		case CA_KEY_3: return ImGuiKey_3;
		case CA_KEY_4: return ImGuiKey_4;
		case CA_KEY_5: return ImGuiKey_5;
		case CA_KEY_6: return ImGuiKey_6;
		case CA_KEY_7: return ImGuiKey_7;
		case CA_KEY_8: return ImGuiKey_8;
		case CA_KEY_9: return ImGuiKey_9;
		case CA_KEY_A: return ImGuiKey_A;
		case CA_KEY_B: return ImGuiKey_B;
		case CA_KEY_C: return ImGuiKey_C;
		case CA_KEY_D: return ImGuiKey_D;
		case CA_KEY_E: return ImGuiKey_E;
		case CA_KEY_F: return ImGuiKey_F;
		case CA_KEY_G: return ImGuiKey_G;
		case CA_KEY_H: return ImGuiKey_H;
		case CA_KEY_I: return ImGuiKey_I;
		case CA_KEY_J: return ImGuiKey_J;
		case CA_KEY_K: return ImGuiKey_K;
		case CA_KEY_L: return ImGuiKey_L;
		case CA_KEY_M: return ImGuiKey_M;
		case CA_KEY_N: return ImGuiKey_N;
		case CA_KEY_O: return ImGuiKey_O;
		case CA_KEY_P: return ImGuiKey_P;
		case CA_KEY_Q: return ImGuiKey_Q;
		case CA_KEY_R: return ImGuiKey_R;
		case CA_KEY_S: return ImGuiKey_S;
		case CA_KEY_T: return ImGuiKey_T;
		case CA_KEY_U: return ImGuiKey_U;
		case CA_KEY_V: return ImGuiKey_V;
		case CA_KEY_W: return ImGuiKey_W;
		case CA_KEY_X: return ImGuiKey_X;
		case CA_KEY_Y: return ImGuiKey_Y;
		case CA_KEY_Z: return ImGuiKey_Z;
		case CA_KEY_F1: return ImGuiKey_F1;
		case CA_KEY_F2: return ImGuiKey_F2;
		case CA_KEY_F3: return ImGuiKey_F3;
		case CA_KEY_F4: return ImGuiKey_F4;
		case CA_KEY_F5: return ImGuiKey_F5;
		case CA_KEY_F6: return ImGuiKey_F6;
		case CA_KEY_F7: return ImGuiKey_F7;
		case CA_KEY_F8: return ImGuiKey_F8;
		case CA_KEY_F9: return ImGuiKey_F9;
		case CA_KEY_F10: return ImGuiKey_F10;
		case CA_KEY_F11: return ImGuiKey_F11;
		case CA_KEY_F12: return ImGuiKey_F12;
		case CA_KEY_F13: return ImGuiKey_F13;
		case CA_KEY_F14: return ImGuiKey_F14;
		case CA_KEY_F15: return ImGuiKey_F15;
		case CA_KEY_F16: return ImGuiKey_F16;
		case CA_KEY_F17: return ImGuiKey_F17;
		case CA_KEY_F18: return ImGuiKey_F18;
		case CA_KEY_F19: return ImGuiKey_F19;
		case CA_KEY_F20: return ImGuiKey_F20;
		case CA_KEY_F21: return ImGuiKey_F21;
		case CA_KEY_F22: return ImGuiKey_F22;
		case CA_KEY_F23: return ImGuiKey_F23;
		case CA_KEY_F24: return ImGuiKey_F24;
		default: return ImGuiKey_None;
		}
	}

	// X11 does not include current pressed/released modifier key in 'mods' flags submitted by GLFW
	// See https://github.com/ocornut/imgui/issues/6034 and https://github.com/glfw/glfw/issues/1630
	static void ImGui_ImplGlfw_UpdateKeyModifiers(IWindow* window)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.AddKeyEvent(ImGuiMod_Ctrl, window->GetKeyState(CA_KEY_LEFT_CONTROL, CA_PRESS) || window->GetKeyState(CA_KEY_RIGHT_CONTROL, CA_PRESS));
		io.AddKeyEvent(ImGuiMod_Shift, window->GetKeyState(CA_KEY_LEFT_SHIFT, CA_PRESS) || window->GetKeyState(CA_KEY_RIGHT_SHIFT, CA_PRESS));
		io.AddKeyEvent(ImGuiMod_Alt, window->GetKeyState(CA_KEY_LEFT_ALT, CA_PRESS) || window->GetKeyState(CA_KEY_RIGHT_ALT, CA_PRESS));
		io.AddKeyEvent(ImGuiMod_Super, window->GetKeyState(CA_KEY_LEFT_SUPER, CA_PRESS) || window->GetKeyState(CA_KEY_RIGHT_SUPER, CA_PRESS));
	}

	void ImGui_ImplGlfw_KeyCallback(IWindow* window, int keycode, int scancode, int action, int mods)
	{
		if (action != CA_PRESS && action != CA_RELEASE)
			return;

		ImGui_ImplGlfw_UpdateKeyModifiers(window);

		ImGuiIO& io = ImGui::GetIO();
		ImGuiKey imgui_key = ImGui_ImplGlfw_KeyToImGuiKey(keycode);
		io.AddKeyEvent(imgui_key, (action == CA_PRESS));
		io.SetKeyEventNativeData(imgui_key, keycode, scancode); // To support legacy indexing (<1.87 user code)
	}

	void ImGui_ImplGlfw_WindowFocusCallback(IWindow* window, bool focused)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.AddFocusEvent(focused);
	}

	void ImGui_ImplGlfw_CursorPosCallback(IWindow* window, float x, float y)
	{
		IMGUIContext* bd = GetIMGUIContext();
		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			int posX, posY;
			window->GetWindowPos(posX, posY);
			x += posX;
			y += posY;
		}
		io.AddMousePosEvent((float)x, (float)y);
		bd->m_LastValidMousePos = ImVec2((float)x, (float)y);
	}

	// Workaround: X11 seems to send spurious Leave/Enter events which would make us lose our position,
	// so we back it up and restore on Leave/Enter (see https://github.com/ocornut/imgui/issues/4984)
	void ImGui_ImplGlfw_CursorEnterCallback(IWindow* window, int entered)
	{
		IMGUIContext* bd = GetIMGUIContext();

		ImGuiIO& io = ImGui::GetIO();
		if (entered)
		{
			bd->m_MouseWindow = window;
			io.AddMousePosEvent(bd->m_LastValidMousePos.x, bd->m_LastValidMousePos.y);
		}
		else if (!entered && bd->m_MouseWindow == window)
		{
			bd->m_LastValidMousePos = io.MousePos;
			bd->m_MouseWindow = nullptr;
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
		}
	}

	void ImGui_ImplGlfw_CharCallback(IWindow* window, unsigned int c)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.AddInputCharacter(c);
	}

	void ImGui_ImplGlfw_MouseButtonCallback(IWindow* window, int button, int action, int mods)
	{

		ImGui_ImplGlfw_UpdateKeyModifiers(window);

		ImGuiIO& io = ImGui::GetIO();
		if (button >= 0 && button < ImGuiMouseButton_COUNT)
			io.AddMouseButtonEvent(button, action == CA_PRESS);
	}

	void ImGui_ImplGlfw_ScrollCallback(IWindow* window, float xoffset, float yoffset)
	{

#ifdef __EMSCRIPTEN__
		// Ignore GLFW events: will be processed in ImGui_ImplEmscripten_WheelCallback().
		return;
#endif

		ImGuiIO& io = ImGui::GetIO();
		io.AddMouseWheelEvent((float)xoffset, (float)yoffset);
	}

	static void ImGui_ImplGlfw_WindowCloseCallback(IWindow* window)
	{
		if (ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle(window))
		{
			if (viewport == nullptr)
				return;
			viewport->PlatformRequestClose = true;
		}
	}

	// GLFW may dispatch window pos/size events after calling glfwSetWindowPos()/glfwSetWindowSize().
	// However: depending on the platform the callback may be invoked at different time:
	// - on Windows it appears to be called within the glfwSetWindowPos()/glfwSetWindowSize() call
	// - on Linux it is queued and invoked during glfwPollEvents()
	// Because the event doesn't always fire on glfwSetWindowXXX() we use a frame counter tag to only
	// ignore recent glfwSetWindowXXX() calls.
	static void ImGui_ImplGlfw_WindowPosCallback(IWindow* window, int x, int y)
	{
		if (ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle(window))
		{
			if (IMGUIViewportContext* vd = (IMGUIViewportContext*)viewport->PlatformUserData)
			{
				bool ignore_event = (ImGui::GetFrameCount() <= vd->IgnoreWindowPosEventFrame + 1);
				if (ignore_event)
					return;
			}
			viewport->PlatformRequestMove = true;
		}
	}

	static void ImGui_ImplGlfw_WindowSizeCallback(IWindow* window, int width, int height)
	{
		if (ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle(window))
		{
			if (viewport == nullptr)
				return;
			if (IMGUIViewportContext* vd = (IMGUIViewportContext*)viewport->PlatformUserData)
			{
				bool ignore_event = (ImGui::GetFrameCount() <= vd->IgnoreWindowSizeEventFrame + 1);
				//data->IgnoreWindowSizeEventFrame = -1;
				if (ignore_event)
					return;
			}
			viewport->PlatformRequestResize = true;
		}
	}


	void DrawFrame(int id)
	{
		ImGui::Begin(("FrameID" + castl::to_string(id)).c_str(), 0, 0);
		ImGui::Text("FrameID %d", ImGui::GetFrameCount());
		ImGui::End();
	}

	void IMGUIContext::DrawView(int id)
	{

		ImGui::Begin("View");

		ImVec2 vMin = ImGui::GetWindowContentRegionMin();
		ImVec2 vMax = ImGui::GetWindowContentRegionMax();

		vMin.x += ImGui::GetWindowPos().x;
		vMin.y += ImGui::GetWindowPos().y;
		vMax.x += ImGui::GetWindowPos().x;
		vMax.y += ImGui::GetWindowPos().y;

		//ImGui::GetForegroundDrawList()->AddRect(vMin, vMax, IM_COL32(255, 255, 0, 255));
		m_TextureViewContexts.push_back(IMGUITextureViewContext{ {}, {vMin.x, vMin.y, vMax.x - vMin.x, vMax.y - vMin.y}, {}, id });
		ImTextureID texID = &m_TextureViewContexts.back();
		ImGui::Image(texID, ImVec2(vMax.x - vMin.x, vMax.y - vMin.y), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
		ImGui::End();

	}

	void ShowExampleAppDockSpace(bool* p_open)
	{
		// READ THIS !!!
		// TL;DR; this demo is more complicated than what most users you would normally use.
		// If we remove all options we are showcasing, this demo would become:
		//     void ShowExampleAppDockSpace()
		//     {
		//         ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
		//     }
		// In most cases you should be able to just call DockSpaceOverViewport() and ignore all the code below!
		// In this specific demo, we are not using DockSpaceOverViewport() because:
		// - (1) we allow the host window to be floating/moveable instead of filling the viewport (when opt_fullscreen == false)
		// - (2) we allow the host window to have padding (when opt_padding == true)
		// - (3) we expose many flags and need a way to have them visible.
		// - (4) we have a local menu bar in the host window (vs. you could use BeginMainMenuBar() + DockSpaceOverViewport()
		//      in your code, but we don't here because we allow the window to be floating)

		static bool opt_fullscreen = true;
		static bool opt_padding = false;
		static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

		// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
		// because it would be confusing to have two docking targets within each others.
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		if (opt_fullscreen)
		{
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->WorkPos);
			ImGui::SetNextWindowSize(viewport->WorkSize);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}
		else
		{
			dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
		}

		// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
		// and handle the pass-thru hole, so we ask Begin() to not render a background.
		if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
			window_flags |= ImGuiWindowFlags_NoBackground;

		// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
		// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
		// all active windows docked into it will lose their parent and become undocked.
		// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
		// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
		if (!opt_padding)
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("DockSpace Demo", p_open, window_flags);
		if (!opt_padding)
			ImGui::PopStyleVar();

		if (opt_fullscreen)
			ImGui::PopStyleVar(2);

		// Submit the DockSpace
		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
		}
		else
		{
			//ShowDockingDisabledMessage();
		}

		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("Options"))
			{
				// Disabling fullscreen would allow the window to be moved to the front of other windows,
				// which we can't undo at the moment without finer window depth/z control.
				ImGui::MenuItem("Fullscreen", NULL, &opt_fullscreen);
				ImGui::MenuItem("Padding", NULL, &opt_padding);
				ImGui::Separator();

				if (ImGui::MenuItem("Flag: NoDockingOverCentralNode", "", (dockspace_flags & ImGuiDockNodeFlags_NoDockingOverCentralNode) != 0)) { dockspace_flags ^= ImGuiDockNodeFlags_NoDockingOverCentralNode; }
				if (ImGui::MenuItem("Flag: NoDockingSplit", "", (dockspace_flags & ImGuiDockNodeFlags_NoDockingSplit) != 0)) { dockspace_flags ^= ImGuiDockNodeFlags_NoDockingSplit; }
				if (ImGui::MenuItem("Flag: NoUndocking", "", (dockspace_flags & ImGuiDockNodeFlags_NoUndocking) != 0)) { dockspace_flags ^= ImGuiDockNodeFlags_NoUndocking; }
				if (ImGui::MenuItem("Flag: NoResize", "", (dockspace_flags & ImGuiDockNodeFlags_NoResize) != 0)) { dockspace_flags ^= ImGuiDockNodeFlags_NoResize; }
				if (ImGui::MenuItem("Flag: AutoHideTabBar", "", (dockspace_flags & ImGuiDockNodeFlags_AutoHideTabBar) != 0)) { dockspace_flags ^= ImGuiDockNodeFlags_AutoHideTabBar; }
				if (ImGui::MenuItem("Flag: PassthruCentralNode", "", (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode) != 0, opt_fullscreen)) { dockspace_flags ^= ImGuiDockNodeFlags_PassthruCentralNode; }
				ImGui::Separator();

				if (ImGui::MenuItem("Close", NULL, false, p_open != NULL))
					*p_open = false;
				ImGui::EndMenu();
			}
			//HelpMarker(
			//	"When docking is enabled, you can ALWAYS dock MOST window into another! Try it now!" "\n"
			//	"- Drag from window title bar or their tab to dock/undock." "\n"
			//	"- Drag from window menu button (upper-left button) to undock an entire node (all windows)." "\n"
			//	"- Hold SHIFT to disable docking (if io.ConfigDockingWithShift == false, default)" "\n"
			//	"- Hold SHIFT to enable docking (if io.ConfigDockingWithShift == true)" "\n"
			//	"This demo app has nothing to do with enabling docking!" "\n\n"
			//	"This demo app only demonstrate the use of ImGui::DockSpace() which allows you to manually create a docking node _within_ another window." "\n\n"
			//	"Read comments in ShowExampleAppDockSpace() for more details.");

			ImGui::EndMenuBar();
		}

		ImGui::End();
	}

	IMGUIContext::IMGUIContext() :
		m_ImguiShaderConstantsBuilder("IMGUIConstants"),
		m_ImguiShaderBindingBuilder("IMGUIBinding"),
		p_RenderBackend(nullptr)
	{
		m_ImguiShaderConstantsBuilder
			.Vec4<float>("IMGUIScale_Pos");
		m_ImguiShaderBindingBuilder.ConstantBuffer(m_ImguiShaderConstantsBuilder)
			.Texture2D<float, 4>("FontTexture")
			.SamplerState("FontSampler");
	}
	void IMGUIContext::Initialize(
		castl::shared_ptr<CRenderBackend> const& renderBackend
		, castl::shared_ptr<IWindowSystem> const& windowSystem
		, castl::shared_ptr<IWindow> const& mainWindowHandle
		, ResourceManagingSystem* resourceSystem
		, GPUGraph* initializeGraph
	)
	{
		p_RenderBackend = renderBackend;
		p_WindowSystem = windowSystem;
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.BackendPlatformUserData = this;
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;
		io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
		unsigned char* fontData;
		int texWidth, texHeight;
		io.Fonts->GetTexDataAsAlpha8(&fontData, &texWidth, &texHeight);

		GPUTextureDescriptor fontDesc{};
		fontDesc.accessType = ETextureAccessType::eSampled | ETextureAccessType::eTransferDst;
		fontDesc.format = ETextureFormat::E_R8_UNORM;
		fontDesc.width = texWidth;
		fontDesc.height = texHeight;
		fontDesc.layers = 1;
		fontDesc.mipLevels = 1;
		fontDesc.textureType = ETextureType::e2D;
		m_Fontimage = renderBackend->CreateGPUTexture(fontDesc);

		initializeGraph->ScheduleData(ImageHandle{ m_Fontimage }, fontData, texWidth * texHeight * sizeof(uint8_t));

		resourceSystem->LoadResource<ShaderResrouce>("Shaders/Imgui.shaderbundle", [this](ShaderResrouce* result)
		{
			m_ImguiShaderSet = result;
		});

		InitImGUIPlatformFunctors();

		ImGuiViewport* main_viewport = ImGui::GetMainViewport();

		PrepareInitViewportContext(main_viewport, mainWindowHandle, true);

		//Register Callbacks
		windowSystem->SetWindowFocusCallback(ImGui_ImplGlfw_WindowFocusCallback);
		windowSystem->SetCursorEnterCallback(ImGui_ImplGlfw_CursorEnterCallback);
		windowSystem->SetCursorPosCallback(ImGui_ImplGlfw_CursorPosCallback);
		windowSystem->SetMouseButtonCallback(ImGui_ImplGlfw_MouseButtonCallback);
		windowSystem->SetScrollCallback(ImGui_ImplGlfw_ScrollCallback);
		windowSystem->SetKeyCallback(ImGui_ImplGlfw_KeyCallback);
		windowSystem->SetCharCallback(ImGui_ImplGlfw_CharCallback);
		windowSystem->SetWindowCloseCallback(ImGui_ImplGlfw_WindowCloseCallback);
		windowSystem->SetWindowPosCallback(ImGui_ImplGlfw_WindowPosCallback);
		windowSystem->SetWindowSizeCallback(ImGui_ImplGlfw_WindowSizeCallback);
	}



	void ImGui_Impl_CreateWindow(ImGuiViewport* viewport)
	{
		auto windowSystem = GetIMGUIContext()->GetWindowSystem();
		auto backend = GetIMGUIContext()->GetRenderBackend();
		auto window = windowSystem->NewWindow(viewport->Size.x, viewport->Size.y, ""
			, false, false
			, (viewport->Flags & ImGuiViewportFlags_NoDecoration) ? false : true
			, (viewport->Flags & ImGuiViewportFlags_TopMost) ? true : false).lock();
		GetIMGUIContext()->PrepareInitViewportContext(viewport, window);
	}

	void ImGui_Impl_DestroyWindow(ImGuiViewport* viewport)
	{
		GetIMGUIContext()->ReleaseViewportContext(viewport);
	}
	void ImGui_Impl_ShowWindow(ImGuiViewport* viewport)
	{
		IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewport->PlatformUserData;
		pUserData->pWindowHandle->ShowWindow();
	}

	ImVec2 ImGui_Impl_GetWindowPos(ImGuiViewport* viewport)
	{
		IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewport->PlatformUserData;
		int posX, posY;
		pUserData->pWindowHandle->GetWindowPos(posX, posY);
		return ImVec2((float)posX, (float)posY);
	}


	void ImGui_Impl_SetWindowPos(ImGuiViewport* viewport, ImVec2 pos)
	{
		IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewport->PlatformUserData;
		pUserData->IgnoreWindowPosEventFrame = ImGui::GetFrameCount();
		pUserData->pWindowHandle->SetWindowPos(pos.x, pos.y);
	}


	ImVec2 ImGui_Impl_GetWindowSize(ImGuiViewport* viewport)
	{
		IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewport->PlatformUserData;
		pUserData->IgnoreWindowSizeEventFrame = ImGui::GetFrameCount();
		int sizeX, sizeY;
		pUserData->pWindowHandle->GetWindowSize(sizeX, sizeY);
		return ImVec2((float)sizeX, (float)sizeY);
	}

	static void ImGui_Impl_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
	{
		IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewport->PlatformUserData;
		pUserData->pWindowHandle->SetWindowSize(size.x, size.y);
	}

	void ImGui_Impl_SetWindowFocus(ImGuiViewport* viewport)
	{
		IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewport->PlatformUserData;
		pUserData->pWindowHandle->Focus();
	}

	bool ImGui_Impl_GetWindowFocus(ImGuiViewport* viewport)
	{
		IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewport->PlatformUserData;
		return pUserData->pWindowHandle->GetWindowFocus();
	}

	static bool ImGui_Impl_GetWindowMinimized(ImGuiViewport* viewport)
	{
		IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewport->PlatformUserData;
		return pUserData->pWindowHandle->GetWindowMinimized();
	}

	static void ImGui_Impl_SetWindowTitle(ImGuiViewport* viewport, const char* title)
	{
		IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewport->PlatformUserData;
		pUserData->pWindowHandle->SetWindowName(title);
	}

	void InitImGUIPlatformFunctors()
	{
		ImGuiIO& io = ImGui::GetIO();
		auto& platform_io = ImGui::GetPlatformIO();
		platform_io.Platform_CreateWindow = ImGui_Impl_CreateWindow;
		platform_io.Platform_DestroyWindow = ImGui_Impl_DestroyWindow;
		platform_io.Platform_ShowWindow = ImGui_Impl_ShowWindow;
		platform_io.Platform_SetWindowPos = ImGui_Impl_SetWindowPos;
		platform_io.Platform_GetWindowPos = ImGui_Impl_GetWindowPos;
		platform_io.Platform_SetWindowSize = ImGui_Impl_SetWindowSize;
		platform_io.Platform_GetWindowSize = ImGui_Impl_GetWindowSize;
		platform_io.Platform_SetWindowFocus = ImGui_Impl_SetWindowFocus;
		platform_io.Platform_GetWindowFocus = ImGui_Impl_GetWindowFocus;
		platform_io.Platform_GetWindowMinimized = ImGui_Impl_GetWindowMinimized;
		platform_io.Platform_SetWindowTitle = ImGui_Impl_SetWindowTitle;
		//platform_io.Platform_RenderWindow = ImGui_ImplGlfw_RenderWindow;
		//platform_io.Platform_SwapBuffers = ImGui_ImplGlfw_SwapBuffers;
	//#if GLFW_HAS_WINDOW_ALPHA
	//	platform_io.Platform_SetWindowAlpha = ImGui_ImplGlfw_SetWindowAlpha;
	//#endif
	//#if GLFW_HAS_VULKAN
	//	platform_io.Platform_CreateVkSurface = ImGui_ImplGlfw_CreateVkSurface;
	//#endif
	}

	void IMGUIContext::Release()
	{
		m_Fontimage = {};
		ImGui::DestroyContext();
	}

	void IMGUIContext::UpdateIMGUI()
	{
		m_TextureViewContexts.clear();
		NewFrame();
		auto& io = ImGui::GetIO();

		ImGuiViewport* main_viewport = ImGui::GetMainViewport();
		IMGUIViewportContext* pUserData = (IMGUIViewportContext*)main_viewport->PlatformUserData;
		
		int sizeX, sizeY;
		pUserData->pWindowHandle->GetWindowSize(sizeX, sizeY);
		io.DisplaySize = ImVec2(sizeX, sizeY);
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);


		ImGui::NewFrame();
		//SRS - ShowDemoWindow() sets its own initial position and size, cannot override here
		bool show = true;
		ShowExampleAppDockSpace(&show);
		//ImGui::ShowDemoWindow();
		DrawView(0);

		//DrawFrame(0);
		//DrawFrame(1);

		ImGui::Render();

		//Multi-Viewport
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
		}
	}

	void IMGUIContext::PrepareDrawData(GPUGraph* pRenderGraph)
	{
		auto& io = ImGui::GetIO();
		m_WindowHandles.clear();
		uint32_t handleID = 0;
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
			for (uint32_t i = 0; i < platform_io.Viewports.Size; i++)
			{
				ImGuiViewport* viewport = platform_io.Viewports[i];
				PrepareSingleViewGUIResources(handleID, viewport, pRenderGraph);
			}
		}
	}

	void IMGUIContext::Draw(GPUGraph* pRenderGraph)
	{
		auto& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
			for (int i = 0; i < platform_io.Viewports.Size; i++)
			{
				ImGuiViewport* viewport = platform_io.Viewports[i];
				DrawSingleView(viewport, pRenderGraph);
			}
		}
	}

	void IMGUIContext::NewFrame()
	{
		ImGuiIO& io = ImGui::GetIO();
		ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
		auto monitorCount = p_WindowSystem->GetMonitorCount();

		platform_io.Monitors.resize(0);
		for (uint32_t i = 0; i < monitorCount; ++i)
		{
			auto monitorHandle = p_WindowSystem->GetMonitor(i);
			ImGuiPlatformMonitor monitor;
			monitor.MainPos = ImVec2(monitorHandle.m_MonitorRect.x, monitorHandle.m_MonitorRect.y);
			monitor.MainSize = ImVec2(monitorHandle.m_MonitorRect.width, monitorHandle.m_MonitorRect.height);
			monitor.WorkPos = ImVec2(monitorHandle.m_WorkAreaRect.x, monitorHandle.m_WorkAreaRect.y);
			monitor.WorkSize = ImVec2(monitorHandle.m_WorkAreaRect.width, monitorHandle.m_WorkAreaRect.height);
			monitor.DpiScale = monitorHandle.m_DPIScale;
			platform_io.Monitors.push_back(monitor);
		}
	}

	void IMGUIContext::PrepareSingleViewGUIResources(uint32_t& inoutHandleID, ImGuiViewport* viewPort, GPUGraph* renderGraph)
	{
		ImGuiIO& io = ImGui::GetIO();
		glm::vec4 meshScale_Pos = glm::vec4(2.0f / viewPort->Size.x, 2.0f / viewPort->Size.y, viewPort->Pos.x, viewPort->Pos.y);
		ImDrawData* imDrawData = viewPort->DrawData;
		IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewPort->PlatformUserData;
		pUserData->Reset();

		pUserData->m_Draw = !(viewPort->Flags & ImGuiViewportFlags_IsMinimized);
		if (!pUserData->m_Draw)
		{
			return;
		}

		size_t vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
		size_t indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

		if ((vertexBufferSize == 0) || (indexBufferSize == 0)) {
			pUserData->m_Draw = false;
		}

		if (!pUserData->m_Draw)
		{
			return;
		}

		m_WindowHandles.push_back(pUserData->pWindowSurface);
		auto shaderArgs = castl::make_shared<ShaderArgList>();
		pUserData->m_ShaderArgs = shaderArgs;
		shaderArgs->SetValue("IMGUIScale_Pos", meshScale_Pos);
		shaderArgs->SetSampler("IMGUITextureSampler", TextureSamplerDescriptor::Create());

		auto defaultImageArgs = castl::make_shared<ShaderArgList>();
		defaultImageArgs->SetImage("IMGUITexture", m_Fontimage
			, GPUTextureView::CreateDefaultForSampling(ETextureFormat::E_R8_UNORM, GPUTextureSwizzle::SingleChannel(EColorChannel::eR)));

		pUserData->m_VertexBuffer = BufferHandle("IMGUI Buffer Handle", inoutHandleID++);
		pUserData->m_IndexBuffer = BufferHandle("IMGUI Index Buffer Handle", inoutHandleID++);
		renderGraph->AllocBuffer(pUserData->m_VertexBuffer, GPUBufferDescriptor::Create(EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, imDrawData->TotalVtxCount, sizeof(ImDrawVert)));
		renderGraph->AllocBuffer(pUserData->m_IndexBuffer, GPUBufferDescriptor::Create(EBufferUsage::eIndexBuffer | EBufferUsage::eDataDst, imDrawData->TotalIdxCount, sizeof(ImDrawIdx)));

		size_t vtxOffset = 0;
		size_t idxOffset = 0;
		for (int n = 0; n < imDrawData->CmdListsCount; n++) {
			const ImDrawList* cmd_list = imDrawData->CmdLists[n];


			renderGraph->ScheduleData(pUserData->m_VertexBuffer, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), vtxOffset * sizeof(ImDrawVert));
			renderGraph->ScheduleData(pUserData->m_IndexBuffer, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), idxOffset * sizeof(ImDrawIdx));

			for (int j = 0; j < cmd_list->CmdBuffer.Size; ++j)
			{
				const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
				pUserData->m_IndexDataOffsets.push_back(castl::make_tuple(idxOffset, vtxOffset, pcmd->ElemCount));
				pUserData->m_Sissors.push_back(glm::ivec4(pcmd->ClipRect.x - viewPort->Pos.x, pcmd->ClipRect.y - viewPort->Pos.y, pcmd->ClipRect.z - pcmd->ClipRect.x, pcmd->ClipRect.w - pcmd->ClipRect.y));
				idxOffset += pcmd->ElemCount;
				ImTextureID id = pcmd->GetTexID();
				if (id != nullptr)
				{
					IMGUITextureViewContext* textureContext = (IMGUITextureViewContext*)id;
					if (textureContext->m_ViewportRect.width > 0 && textureContext->m_ViewportRect.height > 0)
					{
						textureContext->m_TextureDescriptor = GPUTextureDescriptor::Create((uint32_t)textureContext->m_ViewportRect.width
							, (uint32_t)textureContext->m_ViewportRect.height
							, ETextureFormat::E_R8G8B8A8_UNORM
							, ETextureAccessType::eSampled | ETextureAccessType::eTransferDst | ETextureAccessType::eRT);

						textureContext->m_RenderTarget = ImageHandle("ExtraViewport", inoutHandleID++);
						renderGraph->AllocImage(textureContext->m_RenderTarget, textureContext->m_TextureDescriptor);

						auto customImageArgs = castl::make_shared<ShaderArgList>();
						customImageArgs->SetImage("IMGUITexture", textureContext->m_RenderTarget, GPUTextureView::CreateDefaultForSampling(textureContext->m_TextureDescriptor.format));
						pUserData->m_TextureBindings.push_back(customImageArgs);
					}
					else
					{
						pUserData->m_TextureBindings.push_back(defaultImageArgs);
					}
				}
				else
				{
					pUserData->m_TextureBindings.push_back(defaultImageArgs);
				}
			}
			vtxOffset += cmd_list->VtxBuffer.Size;
		}
	}

	VertexInputsDescriptor vertexInputDesc = VertexInputsDescriptor::Create(
		sizeof(ImDrawVert),
		{
			VertexAttribute::Create(offsetof(ImDrawVert, pos), VertexInputFormat::eR32G32_SFloat, "POSITION")
			, VertexAttribute::Create(offsetof(ImDrawVert, uv), VertexInputFormat::eR32G32_SFloat, "TEXCOORD")
			, VertexAttribute::Create(offsetof(ImDrawVert, col), VertexInputFormat::eR8G8B8A8_UNorm, "COLOR")
		}
	);

	void IMGUIContext::DrawSingleView(ImGuiViewport* viewPort, GPUGraph* renderGraph)
	{
		IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewPort->PlatformUserData;
		if (!pUserData->m_Draw)
			return;
		auto backBuffer = p_RenderBackend->GetWindowHandle(pUserData->pWindowHandle);

		auto renderPass = RenderPass::New(backBuffer)
			.SetPipelineState({ {}, {}, ColorAttachmentsBlendStates::AlphaTransparent()})
			.PushShaderArguments("imguiCommon", pUserData->m_ShaderArgs)
			.SetShaders(m_ImguiShaderSet);

		for (uint32_t i = 0; i < pUserData->m_IndexDataOffsets.size(); ++i)
		{
			auto& sissors = pUserData->m_Sissors[i];
			auto& indexDataOffset = pUserData->m_IndexDataOffsets[i];
			auto bindings = pUserData->m_TextureBindings[i];

			renderPass.DrawCall(
				DrawCallBatch::New()
				.PushArgList(bindings)
				.SetVertexBuffer(vertexInputDesc, pUserData->m_VertexBuffer)
				.SetIndexBuffer(EIndexBufferType::e16, pUserData->m_IndexBuffer, 0)
				.Draw([&](CommandList& commandList)
					{
						commandList.SetSissor(sissors.x, sissors.y, sissors.z, sissors.w);
						commandList.DrawIndexed(castl::get<2>(indexDataOffset), 1, castl::get<0>(indexDataOffset), castl::get<1>(indexDataOffset));
					})
			);
		}
		renderGraph->AddPass(renderPass);
	}



	void IMGUIContext::PrepareInitViewportContext(ImGuiViewport* viewport, castl::shared_ptr<IWindow> const& pWindow, bool mainWindow)
	{
		auto viewportContext = m_ViewportContextPool.Alloc();
		viewportContext->pWindowHandle = pWindow;
		viewportContext->pWindowSurface = p_RenderBackend->GetWindowHandle(pWindow);
		viewportContext->pContext = this;
		viewport->PlatformUserData = viewportContext;
		viewport->PlatformHandle = viewportContext;

		if (!mainWindow)
		{
			ImGui_Impl_SetWindowPos(viewport, viewport->Pos);
			ImGui_Impl_SetWindowSize(viewport, viewport->Size);
		}
	}

	void IMGUIContext::ReleaseViewportContext(ImGuiViewport* viewport)
	{
		IMGUIViewportContext* viewportContext = (IMGUIViewportContext*)viewport->PlatformUserData;
		viewportContext->pWindowHandle->CloseWindow();
		m_ViewportContextPool.Release(viewportContext);
		viewport->PlatformUserData = nullptr;
	}
	
}