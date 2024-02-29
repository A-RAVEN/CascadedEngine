#include <GPUTexture.h>
#include <imgui.h>
#include <glm/mat4x4.hpp>
#include "IMGUIContext.h"
#include "ShaderResource.h"
#include "KeyCodes.h"

using namespace graphics_backend;
using namespace resource_management;
void InitImGUIPlatformFunctors();

struct ImguiUserData
{
	IMGUIContext* pContext;
	WindowHandle* pWindowHandle;
};

IMGUIContext* GetIMGUIContext()
{
	ImGuiIO& io = ImGui::GetIO();
	return static_cast<IMGUIContext*>(io.BackendPlatformUserData);
}

void NewUserData(ImGuiViewport* viewport, graphics_backend::WindowHandle* pWindow)
{
	ImguiUserData* pUserData = new ImguiUserData();
	pUserData->pWindowHandle = pWindow;
	viewport->PlatformUserData = pUserData;
	pUserData->pWindowHandle->SetWindowPos(viewport->Pos.x, viewport->Pos.y);
}

IMGUIContext::IMGUIContext() : 
	m_ImguiShaderConstantsBuilder("IMGUIConstants"),
	m_ImguiShaderBindingBuilder("IMGUIBinding")
{
	m_ImguiShaderConstantsBuilder
		.Vec2<float>("IMGUIScale");
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
	NewUserData(main_viewport, mainWindowHandle);
}





void ImGui_Impl_CreateWindow(ImGuiViewport* viewport)
{
	//ImguiUserData* pUserData = new ImguiUserData();
	//viewport->PlatformUserData = pUserData;
	//auto pBackend = pUserData->pContext->GetRenderBackend();
	//pUserData->pWindowHandle = pBackend->NewWindow(viewport->Size.x, viewport->Size.y, "").get();
	//pUserData->pWindowHandle->SetWindowPos(viewport->Pos.x, viewport->Pos.y);
	auto backend =GetIMGUIContext()->GetRenderBackend();
	NewUserData(viewport, backend->NewWindow(viewport->Size.x, viewport->Size.y, "").get());

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
	ImguiUserData* pUserData = (ImguiUserData*)viewport->PlatformUserData;
	viewport->PlatformUserData = nullptr;
	pUserData->pWindowHandle->CloseWindow();
	delete pUserData;
}
void ImGui_Impl_ShowWindow(ImGuiViewport* viewport)
{
	ImguiUserData* pUserData = (ImguiUserData*)viewport->PlatformUserData;
	pUserData->pWindowHandle->ShowWindow();
}

ImVec2 ImGui_Impl_GetWindowPos(ImGuiViewport* viewport)
{
	ImguiUserData* pUserData = (ImguiUserData*)viewport->PlatformUserData;
	graphics_backend::uint2 windowPos = pUserData->pWindowHandle->GetWindowPos();
	return ImVec2((float)windowPos.x, (float)windowPos.y);
}


void ImGui_Impl_SetWindowPos(ImGuiViewport* viewport, ImVec2 pos)
{
	ImguiUserData* pUserData = (ImguiUserData*)viewport->PlatformUserData;
	pUserData->pWindowHandle->SetWindowPos(pos.x, pos.y);
}


ImVec2 ImGui_Impl_GetWindowSize(ImGuiViewport* viewport)
{
	ImguiUserData* pUserData = (ImguiUserData*)viewport->PlatformUserData;
	graphics_backend::uint2 windowSize = pUserData->pWindowHandle->GetWindowSize();
	return ImVec2((float)windowSize.x, (float)windowSize.y);
}

static void ImGui_Impl_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
{
	ImguiUserData* pUserData = (ImguiUserData*)viewport->PlatformUserData;
	pUserData->pWindowHandle->SetWindowSize(size.x, size.y);
}

void ImGui_Impl_SetWindowFocus(ImGuiViewport* viewport)
{
	ImguiUserData* pUserData = (ImguiUserData*)viewport->PlatformUserData;
	pUserData->pWindowHandle->Focus();
}

bool ImGui_Impl_GetWindowFocus(ImGuiViewport* viewport)
{
	ImguiUserData* pUserData = (ImguiUserData*)viewport->PlatformUserData;
	return pUserData->pWindowHandle->GetWindowFocus();
}

static bool ImGui_Impl_GetWindowMinimized(ImGuiViewport* viewport)
{
	ImguiUserData* pUserData = (ImguiUserData*)viewport->PlatformUserData;
	return pUserData->pWindowHandle->GetWindowMinimized();
}

static void ImGui_Impl_SetWindowTitle(ImGuiViewport* viewport, const char* title)
{
	ImguiUserData* pUserData = (ImguiUserData*)viewport->PlatformUserData;
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

	io.MousePos = ImVec2(windowHandle->GetMouseX(), windowHandle->GetMouseY());
	io.MouseDown[0] = windowHandle->IsMouseDown(CA_MOUSE_BUTTON_LEFT);
	io.MouseDown[1] = windowHandle->IsMouseDown(CA_MOUSE_BUTTON_RIGHT);
	io.MouseDown[2] = windowHandle->IsMouseDown(CA_MOUSE_BUTTON_MIDDLE);
	// Render to generate draw buffers
	ImGui::Render();

	//Multi-Viewport
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
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

void IMGUIContext::DrawIMGUI(graphics_backend::CRenderGraph* renderGraph, graphics_backend::TextureHandle renderTargethandle)
{
	ImGuiIO& io = ImGui::GetIO();
	glm::vec2 meshScale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
	ImDrawData* imDrawData = ImGui::GetDrawData();
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
