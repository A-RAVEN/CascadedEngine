//#include <imgui.h>
//#ifndef IMGUI_DISABLE
////#include "imgui_impl_glfw.h"
////
////// Clang warnings with -Weverything
////#if defined(__clang__)
////#pragma clang diagnostic push
////#pragma clang diagnostic ignored "-Wold-style-cast"     // warning: use of old-style cast
////#pragma clang diagnostic ignored "-Wsign-conversion"    // warning: implicit conversion changes signedness
////#endif
//
////// GLFW
////#include <GLFW/glfw3.h>
////
////#ifdef _WIN32
////#undef APIENTRY
////#define GLFW_EXPOSE_NATIVE_WIN32
////#include <GLFW/glfw3native.h>   // for glfwGetWin32Window()
////#endif
////#ifdef __APPLE__
////#define GLFW_EXPOSE_NATIVE_COCOA
////#include <GLFW/glfw3native.h>   // for glfwGetCocoaWindow()
////#endif
//
////#ifdef __EMSCRIPTEN__
////#include <emscripten.h>
////#include <emscripten/html5.h>
////#endif
//
//// We gather version tests as define in order to easily see which features are version-dependent.
//#define GLFW_VERSION_COMBINED           (GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 + GLFW_VERSION_REVISION)
//#define GLFW_HAS_WINDOW_TOPMOST         (GLFW_VERSION_COMBINED >= 3200) // 3.2+ GLFW_FLOATING
//#define GLFW_HAS_WINDOW_HOVERED         (GLFW_VERSION_COMBINED >= 3300) // 3.3+ GLFW_HOVERED
//#define GLFW_HAS_WINDOW_ALPHA           (GLFW_VERSION_COMBINED >= 3300) // 3.3+ glfwSetWindowOpacity
//#define GLFW_HAS_PER_MONITOR_DPI        (GLFW_VERSION_COMBINED >= 3300) // 3.3+ glfwGetMonitorContentScale
//#if defined(__EMSCRIPTEN__) || defined(__SWITCH__)                      // no Vulkan support in GLFW for Emscripten or homebrew Nintendo Switch
//#define GLFW_HAS_VULKAN                 (0)
//#else
//#define GLFW_HAS_VULKAN                 (GLFW_VERSION_COMBINED >= 3200) // 3.2+ glfwCreateWindowSurface
//#endif
//#define GLFW_HAS_FOCUS_WINDOW           (GLFW_VERSION_COMBINED >= 3200) // 3.2+ glfwFocusWindow
//#define GLFW_HAS_FOCUS_ON_SHOW          (GLFW_VERSION_COMBINED >= 3300) // 3.3+ GLFW_FOCUS_ON_SHOW
//#define GLFW_HAS_MONITOR_WORK_AREA      (GLFW_VERSION_COMBINED >= 3300) // 3.3+ glfwGetMonitorWorkarea
//#define GLFW_HAS_OSX_WINDOW_POS_FIX     (GLFW_VERSION_COMBINED >= 3301) // 3.3.1+ Fixed: Resizing window repositions it on MacOS #1553
//#ifdef GLFW_RESIZE_NESW_CURSOR          // Let's be nice to people who pulled GLFW between 2019-04-16 (3.4 define) and 2019-11-29 (cursors defines) // FIXME: Remove when GLFW 3.4 is released?
//#define GLFW_HAS_NEW_CURSORS            (GLFW_VERSION_COMBINED >= 3400) // 3.4+ GLFW_RESIZE_ALL_CURSOR, GLFW_RESIZE_NESW_CURSOR, GLFW_RESIZE_NWSE_CURSOR, GLFW_NOT_ALLOWED_CURSOR
//#else
//#define GLFW_HAS_NEW_CURSORS            (0)
//#endif
//#ifdef GLFW_MOUSE_PASSTHROUGH           // Let's be nice to people who pulled GLFW between 2019-04-16 (3.4 define) and 2020-07-17 (passthrough)
//#define GLFW_HAS_MOUSE_PASSTHROUGH      (GLFW_VERSION_COMBINED >= 3400) // 3.4+ GLFW_MOUSE_PASSTHROUGH
//#else
//#define GLFW_HAS_MOUSE_PASSTHROUGH      (0)
//#endif
//#define GLFW_HAS_GAMEPAD_API            (GLFW_VERSION_COMBINED >= 3300) // 3.3+ glfwGetGamepadState() new api
//#define GLFW_HAS_GETKEYNAME             (GLFW_VERSION_COMBINED >= 3200) // 3.2+ glfwGetKeyName()
//#define GLFW_HAS_GETERROR               (GLFW_VERSION_COMBINED >= 3300) // 3.3+ glfwGetError()
//
//// Forward Declarations
//static void ImGui_ImplGlfw_UpdateMonitors();
//static void ImGui_ImplGlfw_InitPlatformInterface();
//static void ImGui_ImplGlfw_ShutdownPlatformInterface();
//
////
////static ImGuiKey ImGui_ImplGlfw_KeyToImGuiKey(int key)
////{
////    switch (key)
////    {
////    case GLFW_KEY_TAB: return ImGuiKey_Tab;
////    case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
////    case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
////    case GLFW_KEY_UP: return ImGuiKey_UpArrow;
////    case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
////    case GLFW_KEY_PAGE_UP: return ImGuiKey_PageUp;
////    case GLFW_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
////    case GLFW_KEY_HOME: return ImGuiKey_Home;
////    case GLFW_KEY_END: return ImGuiKey_End;
////    case GLFW_KEY_INSERT: return ImGuiKey_Insert;
////    case GLFW_KEY_DELETE: return ImGuiKey_Delete;
////    case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
////    case GLFW_KEY_SPACE: return ImGuiKey_Space;
////    case GLFW_KEY_ENTER: return ImGuiKey_Enter;
////    case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
////    case GLFW_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
////    case GLFW_KEY_COMMA: return ImGuiKey_Comma;
////    case GLFW_KEY_MINUS: return ImGuiKey_Minus;
////    case GLFW_KEY_PERIOD: return ImGuiKey_Period;
////    case GLFW_KEY_SLASH: return ImGuiKey_Slash;
////    case GLFW_KEY_SEMICOLON: return ImGuiKey_Semicolon;
////    case GLFW_KEY_EQUAL: return ImGuiKey_Equal;
////    case GLFW_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
////    case GLFW_KEY_BACKSLASH: return ImGuiKey_Backslash;
////    case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
////    case GLFW_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
////    case GLFW_KEY_CAPS_LOCK: return ImGuiKey_CapsLock;
////    case GLFW_KEY_SCROLL_LOCK: return ImGuiKey_ScrollLock;
////    case GLFW_KEY_NUM_LOCK: return ImGuiKey_NumLock;
////    case GLFW_KEY_PRINT_SCREEN: return ImGuiKey_PrintScreen;
////    case GLFW_KEY_PAUSE: return ImGuiKey_Pause;
////    case GLFW_KEY_KP_0: return ImGuiKey_Keypad0;
////    case GLFW_KEY_KP_1: return ImGuiKey_Keypad1;
////    case GLFW_KEY_KP_2: return ImGuiKey_Keypad2;
////    case GLFW_KEY_KP_3: return ImGuiKey_Keypad3;
////    case GLFW_KEY_KP_4: return ImGuiKey_Keypad4;
////    case GLFW_KEY_KP_5: return ImGuiKey_Keypad5;
////    case GLFW_KEY_KP_6: return ImGuiKey_Keypad6;
////    case GLFW_KEY_KP_7: return ImGuiKey_Keypad7;
////    case GLFW_KEY_KP_8: return ImGuiKey_Keypad8;
////    case GLFW_KEY_KP_9: return ImGuiKey_Keypad9;
////    case GLFW_KEY_KP_DECIMAL: return ImGuiKey_KeypadDecimal;
////    case GLFW_KEY_KP_DIVIDE: return ImGuiKey_KeypadDivide;
////    case GLFW_KEY_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
////    case GLFW_KEY_KP_SUBTRACT: return ImGuiKey_KeypadSubtract;
////    case GLFW_KEY_KP_ADD: return ImGuiKey_KeypadAdd;
////    case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
////    case GLFW_KEY_KP_EQUAL: return ImGuiKey_KeypadEqual;
////    case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
////    case GLFW_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
////    case GLFW_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
////    case GLFW_KEY_LEFT_SUPER: return ImGuiKey_LeftSuper;
////    case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
////    case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
////    case GLFW_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
////    case GLFW_KEY_RIGHT_SUPER: return ImGuiKey_RightSuper;
////    case GLFW_KEY_MENU: return ImGuiKey_Menu;
////    case GLFW_KEY_0: return ImGuiKey_0;
////    case GLFW_KEY_1: return ImGuiKey_1;
////    case GLFW_KEY_2: return ImGuiKey_2;
////    case GLFW_KEY_3: return ImGuiKey_3;
////    case GLFW_KEY_4: return ImGuiKey_4;
////    case GLFW_KEY_5: return ImGuiKey_5;
////    case GLFW_KEY_6: return ImGuiKey_6;
////    case GLFW_KEY_7: return ImGuiKey_7;
////    case GLFW_KEY_8: return ImGuiKey_8;
////    case GLFW_KEY_9: return ImGuiKey_9;
////    case GLFW_KEY_A: return ImGuiKey_A;
////    case GLFW_KEY_B: return ImGuiKey_B;
////    case GLFW_KEY_C: return ImGuiKey_C;
////    case GLFW_KEY_D: return ImGuiKey_D;
////    case GLFW_KEY_E: return ImGuiKey_E;
////    case GLFW_KEY_F: return ImGuiKey_F;
////    case GLFW_KEY_G: return ImGuiKey_G;
////    case GLFW_KEY_H: return ImGuiKey_H;
////    case GLFW_KEY_I: return ImGuiKey_I;
////    case GLFW_KEY_J: return ImGuiKey_J;
////    case GLFW_KEY_K: return ImGuiKey_K;
////    case GLFW_KEY_L: return ImGuiKey_L;
////    case GLFW_KEY_M: return ImGuiKey_M;
////    case GLFW_KEY_N: return ImGuiKey_N;
////    case GLFW_KEY_O: return ImGuiKey_O;
////    case GLFW_KEY_P: return ImGuiKey_P;
////    case GLFW_KEY_Q: return ImGuiKey_Q;
////    case GLFW_KEY_R: return ImGuiKey_R;
////    case GLFW_KEY_S: return ImGuiKey_S;
////    case GLFW_KEY_T: return ImGuiKey_T;
////    case GLFW_KEY_U: return ImGuiKey_U;
////    case GLFW_KEY_V: return ImGuiKey_V;
////    case GLFW_KEY_W: return ImGuiKey_W;
////    case GLFW_KEY_X: return ImGuiKey_X;
////    case GLFW_KEY_Y: return ImGuiKey_Y;
////    case GLFW_KEY_Z: return ImGuiKey_Z;
////    case GLFW_KEY_F1: return ImGuiKey_F1;
////    case GLFW_KEY_F2: return ImGuiKey_F2;
////    case GLFW_KEY_F3: return ImGuiKey_F3;
////    case GLFW_KEY_F4: return ImGuiKey_F4;
////    case GLFW_KEY_F5: return ImGuiKey_F5;
////    case GLFW_KEY_F6: return ImGuiKey_F6;
////    case GLFW_KEY_F7: return ImGuiKey_F7;
////    case GLFW_KEY_F8: return ImGuiKey_F8;
////    case GLFW_KEY_F9: return ImGuiKey_F9;
////    case GLFW_KEY_F10: return ImGuiKey_F10;
////    case GLFW_KEY_F11: return ImGuiKey_F11;
////    case GLFW_KEY_F12: return ImGuiKey_F12;
////    case GLFW_KEY_F13: return ImGuiKey_F13;
////    case GLFW_KEY_F14: return ImGuiKey_F14;
////    case GLFW_KEY_F15: return ImGuiKey_F15;
////    case GLFW_KEY_F16: return ImGuiKey_F16;
////    case GLFW_KEY_F17: return ImGuiKey_F17;
////    case GLFW_KEY_F18: return ImGuiKey_F18;
////    case GLFW_KEY_F19: return ImGuiKey_F19;
////    case GLFW_KEY_F20: return ImGuiKey_F20;
////    case GLFW_KEY_F21: return ImGuiKey_F21;
////    case GLFW_KEY_F22: return ImGuiKey_F22;
////    case GLFW_KEY_F23: return ImGuiKey_F23;
////    case GLFW_KEY_F24: return ImGuiKey_F24;
////    default: return ImGuiKey_None;
////    }
////}
////
////// X11 does not include current pressed/released modifier key in 'mods' flags submitted by GLFW
////// See https://github.com/ocornut/imgui/issues/6034 and https://github.com/glfw/glfw/issues/1630
////static void ImGui_ImplGlfw_UpdateKeyModifiers(GLFWwindow* window)
////{
////    ImGuiIO& io = ImGui::GetIO();
////    io.AddKeyEvent(ImGuiMod_Ctrl, (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS));
////    io.AddKeyEvent(ImGuiMod_Shift, (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS));
////    io.AddKeyEvent(ImGuiMod_Alt, (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS));
////    io.AddKeyEvent(ImGuiMod_Super, (glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS));
////}
//
////--------------------------------------------------------------------------------------------------------
//// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
//// This is an _advanced_ and _optional_ feature, allowing the backend to create and handle multiple viewports simultaneously.
//// If you are new to dear imgui or creating a new binding for dear imgui, it is recommended that you completely ignore this section first..
////--------------------------------------------------------------------------------------------------------
//
//// Helper structure we store in the void* RendererUserData field of each ImGuiViewport to easily retrieve our backend data.
//
//struct ImGui_ImplGlfw_ViewportData
//{
//    GLFWwindow* Window;
//    bool        WindowOwned;
//    int         IgnoreWindowPosEventFrame;
//    int         IgnoreWindowSizeEventFrame;
//#ifdef _WIN32
//    WNDPROC     PrevWndProc;
//#endif
//
//    ImGui_ImplGlfw_ViewportData() { memset(this, 0, sizeof(*this)); IgnoreWindowSizeEventFrame = IgnoreWindowPosEventFrame = -1; }
//    ~ImGui_ImplGlfw_ViewportData() { IM_ASSERT(Window == nullptr); }
//};
//
////static void ImGui_ImplGlfw_WindowCloseCallback(GLFWwindow* window)
////{
////    if (ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle(window))
////        viewport->PlatformRequestClose = true;
////}
//
//// GLFW may dispatch window pos/size events after calling glfwSetWindowPos()/glfwSetWindowSize().
//// However: depending on the platform the callback may be invoked at different time:
//// - on Windows it appears to be called within the glfwSetWindowPos()/glfwSetWindowSize() call
//// - on Linux it is queued and invoked during glfwPollEvents()
//// Because the event doesn't always fire on glfwSetWindowXXX() we use a frame counter tag to only
//// ignore recent glfwSetWindowXXX() calls.
////static void ImGui_ImplGlfw_WindowPosCallback(GLFWwindow* window, int, int)
////{
////    if (ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle(window))
////    {
////        if (ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData)
////        {
////            bool ignore_event = (ImGui::GetFrameCount() <= vd->IgnoreWindowPosEventFrame + 1);
////            //data->IgnoreWindowPosEventFrame = -1;
////            if (ignore_event)
////                return;
////        }
////        viewport->PlatformRequestMove = true;
////    }
////}
////
////static void ImGui_ImplGlfw_WindowSizeCallback(GLFWwindow* window, int, int)
////{
////    if (ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle(window))
////    {
////        if (ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData)
////        {
////            bool ignore_event = (ImGui::GetFrameCount() <= vd->IgnoreWindowSizeEventFrame + 1);
////            //data->IgnoreWindowSizeEventFrame = -1;
////            if (ignore_event)
////                return;
////        }
////        viewport->PlatformRequestResize = true;
////    }
////}
//
//static void ImGui_ImplGlfw_CreateWindow(ImGuiViewport* viewport)
//{
//    ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
//    ImGui_ImplGlfw_ViewportData* vd = IM_NEW(ImGui_ImplGlfw_ViewportData)();
//    viewport->PlatformUserData = vd;
//
//    // GLFW 3.2 unfortunately always set focus on glfwCreateWindow() if GLFW_VISIBLE is set, regardless of GLFW_FOCUSED
//    // With GLFW 3.3, the hint GLFW_FOCUS_ON_SHOW fixes this problem
//    glfwWindowHint(GLFW_VISIBLE, false);
//    glfwWindowHint(GLFW_FOCUSED, false);
//#if GLFW_HAS_FOCUS_ON_SHOW
//    glfwWindowHint(GLFW_FOCUS_ON_SHOW, false);
//#endif
//    glfwWindowHint(GLFW_DECORATED, (viewport->Flags & ImGuiViewportFlags_NoDecoration) ? false : true);
//#if GLFW_HAS_WINDOW_TOPMOST
//    glfwWindowHint(GLFW_FLOATING, (viewport->Flags & ImGuiViewportFlags_TopMost) ? true : false);
//#endif
//    GLFWwindow* share_window = (bd->ClientApi == GlfwClientApi_OpenGL) ? bd->Window : nullptr;
//    vd->Window = glfwCreateWindow((int)viewport->Size.x, (int)viewport->Size.y, "No Title Yet", nullptr, share_window);
//    vd->WindowOwned = true;
//    viewport->PlatformHandle = (void*)vd->Window;
//#ifdef _WIN32
//    viewport->PlatformHandleRaw = glfwGetWin32Window(vd->Window);
//#elif defined(__APPLE__)
//    viewport->PlatformHandleRaw = (void*)glfwGetCocoaWindow(vd->Window);
//#endif
//    glfwSetWindowPos(vd->Window, (int)viewport->Pos.x, (int)viewport->Pos.y);
//
//    // Install GLFW callbacks for secondary viewports
//    glfwSetWindowFocusCallback(vd->Window, ImGui_ImplGlfw_WindowFocusCallback);
//    glfwSetCursorEnterCallback(vd->Window, ImGui_ImplGlfw_CursorEnterCallback);
//    glfwSetCursorPosCallback(vd->Window, ImGui_ImplGlfw_CursorPosCallback);
//    glfwSetMouseButtonCallback(vd->Window, ImGui_ImplGlfw_MouseButtonCallback);
//    glfwSetScrollCallback(vd->Window, ImGui_ImplGlfw_ScrollCallback);
//    glfwSetKeyCallback(vd->Window, ImGui_ImplGlfw_KeyCallback);
//    glfwSetCharCallback(vd->Window, ImGui_ImplGlfw_CharCallback);
//    glfwSetWindowCloseCallback(vd->Window, ImGui_ImplGlfw_WindowCloseCallback);
//    glfwSetWindowPosCallback(vd->Window, ImGui_ImplGlfw_WindowPosCallback);
//    glfwSetWindowSizeCallback(vd->Window, ImGui_ImplGlfw_WindowSizeCallback);
//    if (bd->ClientApi == GlfwClientApi_OpenGL)
//    {
//        glfwMakeContextCurrent(vd->Window);
//        glfwSwapInterval(0);
//    }
//}
//
//static void ImGui_ImplGlfw_DestroyWindow(ImGuiViewport* viewport)
//{
//    ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
//    if (ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData)
//    {
//        if (vd->WindowOwned)
//        {
//#if !GLFW_HAS_MOUSE_PASSTHROUGH && GLFW_HAS_WINDOW_HOVERED && defined(_WIN32)
//            HWND hwnd = (HWND)viewport->PlatformHandleRaw;
//            ::RemovePropA(hwnd, "IMGUI_VIEWPORT");
//#endif
//
//            // Release any keys that were pressed in the window being destroyed and are still held down,
//            // because we will not receive any release events after window is destroyed.
//            for (int i = 0; i < IM_ARRAYSIZE(bd->KeyOwnerWindows); i++)
//                if (bd->KeyOwnerWindows[i] == vd->Window)
//                    ImGui_ImplGlfw_KeyCallback(vd->Window, i, 0, GLFW_RELEASE, 0); // Later params are only used for main viewport, on which this function is never called.
//
//            glfwDestroyWindow(vd->Window);
//        }
//        vd->Window = nullptr;
//        IM_DELETE(vd);
//    }
//    viewport->PlatformUserData = viewport->PlatformHandle = nullptr;
//}
//
//static void ImGui_ImplGlfw_ShowWindow(ImGuiViewport* viewport)
//{
//    ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
//
//#if defined(_WIN32)
//    // GLFW hack: Hide icon from task bar
//    HWND hwnd = (HWND)viewport->PlatformHandleRaw;
//    if (viewport->Flags & ImGuiViewportFlags_NoTaskBarIcon)
//    {
//        LONG ex_style = ::GetWindowLong(hwnd, GWL_EXSTYLE);
//        ex_style &= ~WS_EX_APPWINDOW;
//        ex_style |= WS_EX_TOOLWINDOW;
//        ::SetWindowLong(hwnd, GWL_EXSTYLE, ex_style);
//    }
//
//    // GLFW hack: install hook for WM_NCHITTEST message handler
//#if !GLFW_HAS_MOUSE_PASSTHROUGH && GLFW_HAS_WINDOW_HOVERED && defined(_WIN32)
//    ::SetPropA(hwnd, "IMGUI_VIEWPORT", viewport);
//    vd->PrevWndProc = (WNDPROC)::GetWindowLongPtr(hwnd, GWLP_WNDPROC);
//    ::SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)ImGui_ImplGlfw_WndProc);
//#endif
//
//#if !GLFW_HAS_FOCUS_ON_SHOW
//    // GLFW hack: GLFW 3.2 has a bug where glfwShowWindow() also activates/focus the window.
//    // The fix was pushed to GLFW repository on 2018/01/09 and should be included in GLFW 3.3 via a GLFW_FOCUS_ON_SHOW window attribute.
//    // See https://github.com/glfw/glfw/issues/1189
//    // FIXME-VIEWPORT: Implement same work-around for Linux/OSX in the meanwhile.
//    if (viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing)
//    {
//        ::ShowWindow(hwnd, SW_SHOWNA);
//        return;
//    }
//#endif
//#endif
//
//    glfwShowWindow(vd->Window);
//}
//
//static ImVec2 ImGui_ImplGlfw_GetWindowPos(ImGuiViewport* viewport)
//{
//    ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
//    int x = 0, y = 0;
//    glfwGetWindowPos(vd->Window, &x, &y);
//    return ImVec2((float)x, (float)y);
//}
//
//static void ImGui_ImplGlfw_SetWindowPos(ImGuiViewport* viewport, ImVec2 pos)
//{
//    ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
//    vd->IgnoreWindowPosEventFrame = ImGui::GetFrameCount();
//    glfwSetWindowPos(vd->Window, (int)pos.x, (int)pos.y);
//}
//
//static ImVec2 ImGui_ImplGlfw_GetWindowSize(ImGuiViewport* viewport)
//{
//    ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
//    int w = 0, h = 0;
//    glfwGetWindowSize(vd->Window, &w, &h);
//    return ImVec2((float)w, (float)h);
//}
//
//static void ImGui_ImplGlfw_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
//{
//    ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
//#if __APPLE__ && !GLFW_HAS_OSX_WINDOW_POS_FIX
//    // Native OS windows are positioned from the bottom-left corner on macOS, whereas on other platforms they are
//    // positioned from the upper-left corner. GLFW makes an effort to convert macOS style coordinates, however it
//    // doesn't handle it when changing size. We are manually moving the window in order for changes of size to be based
//    // on the upper-left corner.
//    int x, y, width, height;
//    glfwGetWindowPos(vd->Window, &x, &y);
//    glfwGetWindowSize(vd->Window, &width, &height);
//    glfwSetWindowPos(vd->Window, x, y - height + size.y);
//#endif
//    vd->IgnoreWindowSizeEventFrame = ImGui::GetFrameCount();
//    glfwSetWindowSize(vd->Window, (int)size.x, (int)size.y);
//}
//
//static void ImGui_ImplGlfw_SetWindowTitle(ImGuiViewport* viewport, const char* title)
//{
//    ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
//    glfwSetWindowTitle(vd->Window, title);
//}
//
//static void ImGui_ImplGlfw_SetWindowFocus(ImGuiViewport* viewport)
//{
//#if GLFW_HAS_FOCUS_WINDOW
//    ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
//    glfwFocusWindow(vd->Window);
//#else
//    // FIXME: What are the effect of not having this function? At the moment imgui doesn't actually call SetWindowFocus - we set that up ahead, will answer that question later.
//    (void)viewport;
//#endif
//}
//
//static bool ImGui_ImplGlfw_GetWindowFocus(ImGuiViewport* viewport)
//{
//    ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
//    return glfwGetWindowAttrib(vd->Window, GLFW_FOCUSED) != 0;
//}
//
//static bool ImGui_ImplGlfw_GetWindowMinimized(ImGuiViewport* viewport)
//{
//    ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
//    return glfwGetWindowAttrib(vd->Window, GLFW_ICONIFIED) != 0;
//}
//
//#if GLFW_HAS_WINDOW_ALPHA
//static void ImGui_ImplGlfw_SetWindowAlpha(ImGuiViewport* viewport, float alpha)
//{
//    ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
//    glfwSetWindowOpacity(vd->Window, alpha);
//}
//#endif
//
//static void ImGui_ImplGlfw_RenderWindow(ImGuiViewport* viewport, void*)
//{
//    ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
//    ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
//    if (bd->ClientApi == GlfwClientApi_OpenGL)
//        glfwMakeContextCurrent(vd->Window);
//}
//
//static void ImGui_ImplGlfw_SwapBuffers(ImGuiViewport* viewport, void*)
//{
//    ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
//    ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
//    if (bd->ClientApi == GlfwClientApi_OpenGL)
//    {
//        glfwMakeContextCurrent(vd->Window);
//        glfwSwapBuffers(vd->Window);
//    }
//}
//
////--------------------------------------------------------------------------------------------------------
//// Vulkan support (the Vulkan renderer needs to call a platform-side support function to create the surface)
////--------------------------------------------------------------------------------------------------------
//
//
//static void ImGui_ImplGlfw_InitPlatformInterface()
//{
//    // Register platform interface (will be coupled with a renderer interface)
//    ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
//    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
//    platform_io.Platform_CreateWindow = ImGui_ImplGlfw_CreateWindow;
//    platform_io.Platform_DestroyWindow = ImGui_ImplGlfw_DestroyWindow;
//    platform_io.Platform_ShowWindow = ImGui_ImplGlfw_ShowWindow;
//    platform_io.Platform_SetWindowPos = ImGui_ImplGlfw_SetWindowPos;
//    platform_io.Platform_GetWindowPos = ImGui_ImplGlfw_GetWindowPos;
//    platform_io.Platform_SetWindowSize = ImGui_ImplGlfw_SetWindowSize;
//    platform_io.Platform_GetWindowSize = ImGui_ImplGlfw_GetWindowSize;
//    platform_io.Platform_SetWindowFocus = ImGui_ImplGlfw_SetWindowFocus;
//    platform_io.Platform_GetWindowFocus = ImGui_ImplGlfw_GetWindowFocus;
//    platform_io.Platform_GetWindowMinimized = ImGui_ImplGlfw_GetWindowMinimized;
//    platform_io.Platform_SetWindowTitle = ImGui_ImplGlfw_SetWindowTitle;
//    platform_io.Platform_RenderWindow = ImGui_ImplGlfw_RenderWindow;
//    platform_io.Platform_SwapBuffers = ImGui_ImplGlfw_SwapBuffers;
//#if GLFW_HAS_WINDOW_ALPHA
//    platform_io.Platform_SetWindowAlpha = ImGui_ImplGlfw_SetWindowAlpha;
//#endif
//#if GLFW_HAS_VULKAN
//    platform_io.Platform_CreateVkSurface = ImGui_ImplGlfw_CreateVkSurface;
//#endif
//
//    // Register main window handle (which is owned by the main application, not by us)
//    // This is mostly for simplicity and consistency, so that our code (e.g. mouse handling etc.) can use same logic for main and secondary viewports.
//    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
//    ImGui_ImplGlfw_ViewportData* vd = IM_NEW(ImGui_ImplGlfw_ViewportData)();
//    vd->Window = bd->Window;
//    vd->WindowOwned = false;
//    main_viewport->PlatformUserData = vd;
//    main_viewport->PlatformHandle = (void*)bd->Window;
//}
//
//static void ImGui_ImplGlfw_ShutdownPlatformInterface()
//{
//    ImGui::DestroyPlatformWindows();
//}
//
////-----------------------------------------------------------------------------
//
//// WndProc hook (declared here because we will need access to ImGui_ImplGlfw_ViewportData) 
//#ifdef _WIN32
//static ImGuiMouseSource GetMouseSourceFromMessageExtraInfo()
//{
//    LPARAM extra_info = ::GetMessageExtraInfo();
//    if ((extra_info & 0xFFFFFF80) == 0xFF515700)
//        return ImGuiMouseSource_Pen;
//    if ((extra_info & 0xFFFFFF80) == 0xFF515780)
//        return ImGuiMouseSource_TouchScreen;
//    return ImGuiMouseSource_Mouse;
//}
//static LRESULT CALLBACK ImGui_ImplGlfw_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
//{
//    ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
//    WNDPROC prev_wndproc = bd->PrevWndProc;
//    ImGuiViewport* viewport = (ImGuiViewport*)::GetPropA(hWnd, "IMGUI_VIEWPORT");
//    if (viewport != NULL)
//        if (ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData)
//            prev_wndproc = vd->PrevWndProc;
//
//    switch (msg)
//    {
//        // GLFW doesn't allow to distinguish Mouse vs TouchScreen vs Pen.
//        // Add support for Win32 (based on imgui_impl_win32), because we rely on _TouchScreen info to trickle inputs differently.
//    case WM_MOUSEMOVE: case WM_NCMOUSEMOVE:
//    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP:
//    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP:
//    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP:
//    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
//        ImGui::GetIO().AddMouseSourceEvent(GetMouseSourceFromMessageExtraInfo());
//        break;
//
//        // We have submitted https://github.com/glfw/glfw/pull/1568 to allow GLFW to support "transparent inputs".
//        // In the meanwhile we implement custom per-platform workarounds here (FIXME-VIEWPORT: Implement same work-around for Linux/OSX!)
//#if !GLFW_HAS_MOUSE_PASSTHROUGH && GLFW_HAS_WINDOW_HOVERED
//    case WM_NCHITTEST:
//    {
//        // Let mouse pass-through the window. This will allow the backend to call io.AddMouseViewportEvent() properly (which is OPTIONAL).
//        // The ImGuiViewportFlags_NoInputs flag is set while dragging a viewport, as want to detect the window behind the one we are dragging.
//        // If you cannot easily access those viewport flags from your windowing/event code: you may manually synchronize its state e.g. in
//        // your main loop after calling UpdatePlatformWindows(). Iterate all viewports/platform windows and pass the flag to your windowing system.
//        if (viewport && (viewport->Flags & ImGuiViewportFlags_NoInputs))
//            return HTTRANSPARENT;
//        break;
//    }
//#endif
//    }
//    return ::CallWindowProc(prev_wndproc, hWnd, msg, wParam, lParam);
//}
//#endif // #ifdef _WIN32
//
////-----------------------------------------------------------------------------
//
//#if defined(__clang__)
//#pragma clang diagnostic pop
//#endif
//
//#endif // #ifndef IMGUI_DISABLE
