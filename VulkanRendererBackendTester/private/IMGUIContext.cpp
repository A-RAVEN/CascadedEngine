#include <GPUTexture.h>
#include <imgui.h>
#include <glm/mat4x4.hpp>
#include "IMGUIContext.h"
#include "ShaderResource.h"
#include "KeyCodes.h"

using namespace graphics_backend;
using namespace resource_management;
void InitImGUIPlatformFunctors();

//struct IMGUIViewportContext
//{
//	IMGUIContext* pContext;
//	WindowHandle* pWindowHandle;
//};

IMGUIContext* GetIMGUIContext()
{
	ImGuiIO& io = ImGui::GetIO();
	return static_cast<IMGUIContext*>(io.BackendPlatformUserData);
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
	m_ImguiShaderSet{},
	p_RenderBackend(nullptr)
{
	m_ImguiShaderConstantsBuilder
		.Vec4<float>("IMGUIScale_Pos");
	m_ImguiShaderBindingBuilder.ConstantBuffer(m_ImguiShaderConstantsBuilder)
		.Texture2D<float, 4>("FontTexture")
		.SamplerState("FontSampler");
}
void IMGUIContext::Initialize(
	CRenderBackend* renderBackend
	, graphics_backend::WindowHandle* mainWindowHandle
	, ResourceManagingSystem* resourceSystem
)
{
	p_RenderBackend = renderBackend;
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.BackendPlatformUserData = this;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
	unsigned char* fontData;
	int texWidth, texHeight;
	io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);

	GPUTextureDescriptor fontDesc{};
	fontDesc.accessType = ETextureAccessType::eSampled | ETextureAccessType::eTransferDst;
	fontDesc.format = ETextureFormat::E_R8G8B8A8_UNORM;
	fontDesc.width = texWidth;
	fontDesc.height = texHeight;
	fontDesc.layers = 1;
	fontDesc.mipLevels = 1;
	fontDesc.textureType = ETextureType::e2D;
	m_Fontimage = renderBackend->CreateGPUTexture(fontDesc);
	m_Fontimage->ScheduleTextureData(0, texWidth * texHeight * 4, fontData);

	m_ImageSampler = renderBackend->GetOrCreateTextureSampler(TextureSamplerDescriptor{});

	resourceSystem->LoadResource<ShaderResrouce>("Shaders/Imgui.shaderbundle", [this](ShaderResrouce* result)
		{
			m_ImguiShaderSet.vert = &result->m_VertexShaderProvider;
			m_ImguiShaderSet.frag = &result->m_FragmentShaderProvider;
		});

	InitImGUIPlatformFunctors();

	ImGuiViewport* main_viewport = ImGui::GetMainViewport();
	PrepareInitViewportContext(main_viewport, mainWindowHandle);
}



void ImGui_Impl_CreateWindow(ImGuiViewport* viewport)
{
	auto backend = GetIMGUIContext()->GetRenderBackend();
	GetIMGUIContext()->PrepareInitViewportContext(viewport, backend->NewWindow(viewport->Size.x, viewport->Size.y, ""
		, false, false
		, (viewport->Flags & ImGuiViewportFlags_NoDecoration) ? false : true
		, (viewport->Flags & ImGuiViewportFlags_TopMost) ? true : false).get());

	/*glfwSetWindowFocusCallback(vd->Window, ImGui_ImplGlfw_WindowFocusCallback);
	glfwSetCursorEnterCallback(vd->Window, ImGui_ImplGlfw_CursorEnterCallback);
	glfwSetCursorPosCallback(vd->Window, ImGui_ImplGlfw_CursorPosCallback);
	glfwSetMouseButtonCallback(vd->Window, ImGui_ImplGlfw_MouseButtonCallback);
	glfwSetScrollCallback(vd->Window, ImGui_ImplGlfw_ScrollCallback);
	glfwSetKeyCallback(vd->Window, ImGui_ImplGlfw_KeyCallback);
	glfwSetCharCallback(vd->Window, ImGui_ImplGlfw_CharCallback);
	glfwSetWindowCloseCallback(vd->Window, ImGui_ImplGlfw_WindowCloseCallback);
	glfwSetWindowPosCallback(vd->Window, ImGui_ImplGlfw_WindowPosCallback);
	glfwSetWindowSizeCallback(vd->Window, ImGui_ImplGlfw_WindowSizeCallback);*/
}

void ImGui_Impl_DestroyWindow(ImGuiViewport* viewport)
{

	IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewport->PlatformUserData;
	viewport->PlatformUserData = nullptr;
	pUserData->pWindowHandle->CloseWindow();
	delete pUserData;
}
void ImGui_Impl_ShowWindow(ImGuiViewport* viewport)
{
	IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewport->PlatformUserData;
	pUserData->pWindowHandle->ShowWindow();
}

ImVec2 ImGui_Impl_GetWindowPos(ImGuiViewport* viewport)
{
	IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewport->PlatformUserData;
	graphics_backend::uint2 windowPos = pUserData->pWindowHandle->GetWindowPos();
	return ImVec2((float)windowPos.x, (float)windowPos.y);
}


void ImGui_Impl_SetWindowPos(ImGuiViewport* viewport, ImVec2 pos)
{
	IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewport->PlatformUserData;
	pUserData->pWindowHandle->SetWindowPos(pos.x, pos.y);
}


ImVec2 ImGui_Impl_GetWindowSize(ImGuiViewport* viewport)
{
	IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewport->PlatformUserData;
	graphics_backend::uint2 windowSize = pUserData->pWindowHandle->GetWindowSize();
	return ImVec2((float)windowSize.x, (float)windowSize.y);
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
	m_ImageSampler.reset();
	m_Fontimage.reset();
	ImGui::DestroyContext();
}

void IMGUIContext::UpdateIMGUI(graphics_backend::WindowHandle const* windowHandle)
{
	NewFrame();
	auto& io = ImGui::GetIO();
	auto windowSize = windowHandle->GetSizeSafe();

	io.DisplaySize = ImVec2(windowSize.x, windowSize.y);
	io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
	ImGui::NewFrame();
	//SRS - ShowDemoWindow() sets its own initial position and size, cannot override here
	ImGui::ShowDemoWindow();
	bool show = true;
	ShowExampleAppDockSpace(&show);
	//ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
	io.MousePos = ImVec2(windowHandle->GetMouseX(), windowHandle->GetMouseY());
	io.MouseDown[0] = windowHandle->IsMouseDown(CA_MOUSE_BUTTON_LEFT);
	io.MouseDown[1] = windowHandle->IsMouseDown(CA_MOUSE_BUTTON_RIGHT);
	io.MouseDown[2] = windowHandle->IsMouseDown(CA_MOUSE_BUTTON_MIDDLE);
	// Render to generate draw buffers
	ImGui::Render();

	//Multi-Viewport
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		//Update Window
		ImGui::UpdatePlatformWindows();
		//TODO: Update GPU Resources For Every Viewport
		//ImGui::RenderPlatformWindowsDefault();
	}
}

void IMGUIContext::PrepareDrawData(graphics_backend::CRenderGraph* pRenderGraph)
{
	auto& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
		for (int i = 0; i < platform_io.Viewports.Size; i++)
		{
			ImGuiViewport* viewport = platform_io.Viewports[i];
			PrepareSingleViewGUIResources(viewport, pRenderGraph);
		}
	}
}

void IMGUIContext::Draw(graphics_backend::CRenderGraph* pRenderGraph)
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
	auto monitorCount = p_RenderBackend->GetMonitorCount();

	platform_io.Monitors.resize(0);
	for (uint32_t i = 0; i < monitorCount; ++i)
	{
		auto monitorHandle = p_RenderBackend->GetMonitorHandleAt(i);
		ImGuiPlatformMonitor monitor;
		monitor.MainPos = ImVec2(monitorHandle.m_MonitorRect.x, monitorHandle.m_MonitorRect.y);
		monitor.MainSize = ImVec2(monitorHandle.m_MonitorRect.width, monitorHandle.m_MonitorRect.height);
		monitor.WorkPos = ImVec2(monitorHandle.m_WorkAreaRect.x, monitorHandle.m_WorkAreaRect.y);
		monitor.WorkSize = ImVec2(monitorHandle.m_WorkAreaRect.width, monitorHandle.m_WorkAreaRect.height);
		monitor.DpiScale = monitorHandle.m_DPIScale;
		platform_io.Monitors.push_back(monitor);
	}
}

void IMGUIContext::PrepareSingleViewGUIResources(ImGuiViewport* viewPort, graphics_backend::CRenderGraph* renderGraph)
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

	pUserData->m_VertexBuffer = renderGraph->NewGPUBufferHandle(EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, imDrawData->TotalVtxCount, sizeof(ImDrawVert));
	pUserData->m_IndexBuffer = renderGraph->NewGPUBufferHandle(EBufferUsage::eIndexBuffer | EBufferUsage::eDataDst, imDrawData->TotalIdxCount, sizeof(ImDrawIdx));

	size_t vtxOffset = 0;
	size_t idxOffset = 0;
	for (int n = 0; n < imDrawData->CmdListsCount; n++) {
		const ImDrawList* cmd_list = imDrawData->CmdLists[n];
		renderGraph->ScheduleBufferData(pUserData->m_VertexBuffer, vtxOffset * sizeof(ImDrawVert), cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), cmd_list->VtxBuffer.Data);
		renderGraph->ScheduleBufferData(pUserData->m_IndexBuffer, idxOffset * sizeof(ImDrawIdx), cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), cmd_list->IdxBuffer.Data);

		for (int j = 0; j < cmd_list->CmdBuffer.Size; ++j)
		{
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
			pUserData->m_IndexDataOffsets.push_back(castl::make_tuple(idxOffset, vtxOffset, pcmd->ElemCount));
			pUserData->m_Sissors.push_back(glm::ivec4(pcmd->ClipRect.x - viewPort->Pos.x, pcmd->ClipRect.y - viewPort->Pos.y, pcmd->ClipRect.z - pcmd->ClipRect.x, pcmd->ClipRect.w - pcmd->ClipRect.y));
			idxOffset += pcmd->ElemCount;
		}
		vtxOffset += cmd_list->VtxBuffer.Size;
	}
	
	pUserData->m_ShaderConstants = renderGraph->NewShaderConstantSetHandle(m_ImguiShaderConstantsBuilder);
	pUserData->m_ShaderBindings = renderGraph->NewShaderBindingSetHandle(m_ImguiShaderBindingBuilder);
	pUserData->m_ShaderBindings.m_Name = "IMGUI Bindings";
	pUserData->m_ShaderConstants.SetValue("IMGUIScale_Pos", meshScale_Pos);
	pUserData->m_ShaderBindings.SetConstantSet(m_ImguiShaderConstantsBuilder.GetName(), pUserData->m_ShaderConstants)
		.SetSampler("FontSampler", m_ImageSampler)
		.SetTexture("FontTexture", m_Fontimage);
}

void IMGUIContext::DrawSingleView(ImGuiViewport* viewPort, graphics_backend::CRenderGraph* renderGraph)
{
	IMGUIViewportContext* pUserData = (IMGUIViewportContext*)viewPort->PlatformUserData;
	if (!pUserData->m_Draw)
		return;
	auto backBuffer = renderGraph->RegisterWindowBackbuffer(pUserData->pWindowHandle);
	CVertexInputDescriptor vertexInputDesc{};
	vertexInputDesc.AddPrimitiveDescriptor(sizeof(ImDrawVert), {
		VertexAttribute{0, offsetof(ImDrawVert, pos), VertexInputFormat::eR32G32_SFloat}
		, VertexAttribute{1, offsetof(ImDrawVert, uv), VertexInputFormat::eR32G32_SFloat}
		, VertexAttribute{2, offsetof(ImDrawVert, col), VertexInputFormat::eR8G8B8A8_UNorm}
		});

	renderGraph->NewRenderPass(backBuffer, GraphicsClearValue::ClearColor(0.0f, 1.0f, 1.0f, 1.0f)
		, CPipelineStateObject{ {}, {RasterizerStates::CullOff()},  ColorAttachmentsBlendStates::AlphaTransparent() }
		, vertexInputDesc
		, m_ImguiShaderSet
		, { {}, {pUserData->m_ShaderBindings} }
		, [pUserData](CInlineCommandList& cmd)
		{
			cmd.SetShaderBindings(castl::vector<ShaderBindingSetHandle>{ pUserData->m_ShaderBindings })
				.BindVertexBuffers({ pUserData->m_VertexBuffer })
				.BindIndexBuffers(EIndexBufferType::e16, pUserData->m_IndexBuffer);
			for (uint32_t i = 0; i < pUserData->m_IndexDataOffsets.size(); ++i)
			{
				auto& sissors = pUserData->m_Sissors[i];
				auto& indexDataOffset = pUserData->m_IndexDataOffsets[i];
				cmd.SetSissor(sissors.x, sissors.y, sissors.z, sissors.w)
					.DrawIndexed(castl::get<2>(indexDataOffset), 1, castl::get<0>(indexDataOffset), castl::get<1>(indexDataOffset));
			}
		});
}

void IMGUIContext::PrepareInitViewportContext(ImGuiViewport* viewport, graphics_backend::WindowHandle* pWindow)
{
	auto viewportContext = m_ViewportContextPool.Alloc();
	viewportContext->pWindowHandle = pWindow;
	viewportContext->pContext = this;
	viewportContext->pWindowHandle->SetWindowPos(viewport->Pos.x, viewport->Pos.y);
	viewport->PlatformUserData = viewportContext;
}

void IMGUIContext::ReleaseViewportContext(ImGuiViewport* viewport)
{
	IMGUIViewportContext* viewportContext = (IMGUIViewportContext*)viewport->PlatformUserData;
	viewport->PlatformUserData = nullptr;
	viewportContext->pWindowHandle->CloseWindow();
	m_ViewportContextPool.Release(viewportContext);
}


void IMGUIContext::DrawIMGUI(graphics_backend::CRenderGraph* renderGraph, graphics_backend::TextureHandle renderTargethandle)
{
	ImGuiIO& io = ImGui::GetIO();
	glm::vec2 meshScale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
	ImGuiViewport* main_viewport = ImGui::GetMainViewport();
	ImDrawData* imDrawData = main_viewport->DrawData;
	size_t vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
	size_t indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

	if ((vertexBufferSize == 0) || (indexBufferSize == 0)) {
		return;
	}

	auto vertexBufferHandle = renderGraph->NewGPUBufferHandle(EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, imDrawData->TotalVtxCount, sizeof(ImDrawVert));
	auto indexBufferHandle = renderGraph->NewGPUBufferHandle(EBufferUsage::eIndexBuffer | EBufferUsage::eDataDst, imDrawData->TotalIdxCount, sizeof(ImDrawIdx));

	CVertexInputDescriptor vertexInputDesc{};
	vertexInputDesc.AddPrimitiveDescriptor(sizeof(ImDrawVert), {
		VertexAttribute{0, offsetof(ImDrawVert, pos), VertexInputFormat::eR32G32_SFloat}
		, VertexAttribute{1, offsetof(ImDrawVert, uv), VertexInputFormat::eR32G32_SFloat}
		, VertexAttribute{2, offsetof(ImDrawVert, col), VertexInputFormat::eR8G8B8A8_UNorm}
		});

	size_t vtxOffset = 0;
	size_t idxOffset = 0;
	castl::vector<castl::tuple<uint32_t, uint32_t, uint32_t>> indexDataOffsets;
	castl::vector<glm::uvec4> sissors;
	for (int n = 0; n < imDrawData->CmdListsCount; n++) {
		const ImDrawList* cmd_list = imDrawData->CmdLists[n];
		renderGraph->ScheduleBufferData(vertexBufferHandle, vtxOffset * sizeof(ImDrawVert), cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), cmd_list->VtxBuffer.Data);
		renderGraph->ScheduleBufferData(indexBufferHandle, idxOffset * sizeof(ImDrawIdx), cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), cmd_list->IdxBuffer.Data);

		for (int j = 0; j < cmd_list->CmdBuffer.Size; ++j)
		{
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
			indexDataOffsets.push_back(castl::make_tuple(idxOffset, vtxOffset, pcmd->ElemCount));
			sissors.push_back(glm::ivec4(pcmd->ClipRect.x, pcmd->ClipRect.y, pcmd->ClipRect.z - pcmd->ClipRect.x, pcmd->ClipRect.w - pcmd->ClipRect.y));
			idxOffset += pcmd->ElemCount;
		}
		vtxOffset += cmd_list->VtxBuffer.Size;
	}

	CAttachmentInfo targetattachmentInfo{};
	targetattachmentInfo.format = renderGraph->GetTextureDescriptor(renderTargethandle).format;
	auto shaderConstants = renderGraph->NewShaderConstantSetHandle(m_ImguiShaderConstantsBuilder);
	auto shaderBinding = renderGraph->NewShaderBindingSetHandle(m_ImguiShaderBindingBuilder);
	shaderConstants.SetValue("IMGUIScale", meshScale);
	shaderBinding.SetConstantSet(m_ImguiShaderConstantsBuilder.GetName(), shaderConstants)
		.SetSampler("FontSampler", m_ImageSampler)
		.SetTexture("FontTexture", m_Fontimage);
	auto blendStates = ColorAttachmentsBlendStates::AlphaTransparent();


	renderGraph->NewRenderPass({ targetattachmentInfo })
		.SetAttachmentTarget(0, renderTargethandle)
		.Subpass({ {0} }
			, CPipelineStateObject{ {}, RasterizerStates::CullOff(), blendStates }
			, vertexInputDesc
			, m_ImguiShaderSet
			, { {}, {shaderBinding} }
			, [shaderBinding, vertexBufferHandle, indexBufferHandle, indexDataOffsets, sissors](CInlineCommandList& cmd)
			{
				cmd.SetShaderBindings(castl::vector<ShaderBindingSetHandle>{ shaderBinding })
					.BindVertexBuffers({ vertexBufferHandle })
					.BindIndexBuffers(EIndexBufferType::e16, indexBufferHandle);
				for (uint32_t i = 0; i < indexDataOffsets.size(); ++i)
				{
					cmd.SetSissor(sissors[i].x, sissors[i].y, sissors[i].z, sissors[i].w)
						.DrawIndexed(castl::get<2>(indexDataOffsets[i]), 1, castl::get<0>(indexDataOffsets[i]), castl::get<1>(indexDataOffsets[i]));
				}
			});
}
