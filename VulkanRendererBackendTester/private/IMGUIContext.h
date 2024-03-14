#pragma once
#include <memory>
#include <CRenderBackend.h>
#include <ShaderProvider.h>
#include <GPUTexture.h>
#include <CAResource/ResourceManagingSystem.h>
#include <ThreadSafePool.h>
#include <imgui.h>
#include <glm/glm.hpp>

struct ImGuiViewport;
class IMGUIContext;
class IMGUIViewportContext
{
public:
	void Initialize() {};
	void Release() {
		m_ViewportTextureHandles.clear();
		pContext = nullptr;
		pWindowHandle = nullptr;
		IgnoreWindowPosEventFrame = -1;
		IgnoreWindowSizeEventFrame = -1;
	};

	void Reset()
	{
		m_ViewportTextureHandles.clear();
		m_IndexDataOffsets.clear();
		m_Sissors.clear();
		m_TextureBindings.clear();
		m_VertexBuffer = {};
		m_IndexBuffer = {};
		m_ShaderConstants = {};
		m_ShaderBindings = {};
	}
public:
	int IgnoreWindowPosEventFrame = -1;
	int IgnoreWindowSizeEventFrame = -1;
public:
	friend class IMGUIContext;
	castl::vector<graphics_backend::TextureHandle> m_ViewportTextureHandles;
	IMGUIContext* pContext;
	graphics_backend::WindowHandle* pWindowHandle;

	graphics_backend::GPUBufferHandle m_VertexBuffer;
	graphics_backend::GPUBufferHandle m_IndexBuffer;
	graphics_backend::ShaderConstantSetHandle m_ShaderConstants;
	graphics_backend::ShaderBindingSetHandle m_ShaderBindings;
	castl::vector<castl::tuple<uint32_t, uint32_t, uint32_t>> m_IndexDataOffsets;
	castl::vector<glm::uvec4> m_Sissors;
	castl::vector<graphics_backend::ShaderBindingSetHandle> m_TextureBindings;
	bool m_Draw;
};

struct IMGUITextureViewContext
{
	graphics_backend::TextureHandle m_RenderTarget;
	graphics_backend::FloatRect m_ViewportRect;
	int m_SceneViewIndex;
};


class IMGUIContext
{
public:
	IMGUIContext();
	void Initialize(
		graphics_backend::CRenderBackend* renderBackend
		, graphics_backend::WindowHandle* mainWindowHandle
		, resource_management::ResourceManagingSystem* resourceSystem
	);
	void Release();
	void UpdateIMGUI();
	void PrepareDrawData(graphics_backend::CRenderGraph* pRenderGraph);
	void Draw(graphics_backend::CRenderGraph* pRenderGraph);
	void DrawIMGUI(
		graphics_backend::CRenderGraph* renderGraph
		, graphics_backend::TextureHandle renderTargethandle);
	void DrawView(int id = 0);
	graphics_backend::CRenderBackend* GetRenderBackend() const { return p_RenderBackend; }
public:
	void PrepareSingleViewGUIResources(ImGuiViewport* viewPort, graphics_backend::CRenderGraph* renderGraph);
	void DrawSingleView(ImGuiViewport* viewPort, graphics_backend::CRenderGraph* renderGraph);
	void PrepareInitViewportContext(ImGuiViewport* viewPort, graphics_backend::WindowHandle* pWindow, bool mainWindow = false);
	void ReleaseViewportContext(ImGuiViewport* viewPort);

	graphics_backend::WindowHandle* m_MouseWindow = nullptr;
	ImVec2 m_LastValidMousePos = ImVec2(-FLT_MAX, -FLT_MAX);
private:
	void NewFrame();

	castl::shared_ptr<graphics_backend::GPUTexture> m_Fontimage;
	castl::shared_ptr<graphics_backend::TextureSampler> m_ImageSampler;
	GraphicsShaderSet m_ImguiShaderSet;
	ShaderConstantsBuilder m_ImguiShaderConstantsBuilder;
	ShaderBindingBuilder m_ImguiShaderBindingBuilder;
	graphics_backend::CRenderBackend* p_RenderBackend;
	threadsafe_utils::TThreadSafePointerPool<IMGUIViewportContext> m_ViewportContextPool;
	castl::deque<IMGUITextureViewContext> m_TextureViewContexts;
};