#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <memory>
#include <CRenderBackend.h>
#include <ShaderProvider.h>
#include <GPUTexture.h>
#include <CAResource/ResourceManagingSystem.h>
#include <ThreadSafePool.h>
#include <glm/glm.hpp>
#include <CAUtils.h>
#include <CAWindow/WindowSystem.h>
#include <CACore/CAModuleManager.h>

namespace imgui_display
{
	using namespace graphics_backend;
	using namespace cawindow;
	class IMGUIContext;
	class IMGUIViewportContext
	{
	public:
		void Initialize() {
			Reset();
		};
		void Release() {
			Reset();
			pContext = nullptr;
			pWindowHandle = nullptr;
			pWindowSurface = nullptr;
			IgnoreWindowPosEventFrame = -1;
			IgnoreWindowSizeEventFrame = -1;
		};

		void Reset()
		{
			m_ViewportTextureHandles.clear();
			m_TextureBindings.clear();
			m_IndexDataOffsets.clear();
			m_Sissors.clear();
			m_VertexBuffer = {};
			m_IndexBuffer = {};
			m_ShaderStruct = nullptr;
		}
	public:
		int IgnoreWindowPosEventFrame = -1;
		int IgnoreWindowSizeEventFrame = -1;
	public:
		friend class IMGUIContext;
		castl::vector<ImageHandle> m_ViewportTextureHandles;
		IMGUIContext* pContext;
		castl::shared_ptr<IWindow> pWindowHandle;
		castl::shared_ptr<WindowHandle> pWindowSurface;

		BufferHandle m_VertexBuffer = {};
		BufferHandle m_IndexBuffer = {};
		castl::shared_ptr<ShaderStruct> m_ShaderStruct = nullptr;
		castl::vector<castl::tuple<uint32_t, uint32_t, uint32_t>> m_IndexDataOffsets;
		castl::vector<glm::uvec4> m_Sissors;
		castl::vector<castl::shared_ptr<ShaderStruct>> m_TextureBindings;
		bool m_Draw = false;
	};

	struct IMGUITextureViewContext
	{
		castl::shared_ptr<IWindow> m_WindowHandle;
		ImageHandle m_RenderTarget;
		cacore::Rect<float> m_ViewportRect;
		GPUTextureDescriptor m_TextureDescriptor;
		int m_SceneViewIndex;
	};


	class IMGUIContext
	{
	public:
		IMGUIContext();
		void Init(cacore::IModuleManager* pModuleManager);
		void Initialize(castl::string const& editorConfigPath
			, castl::shared_ptr<IWindow> const& mainWindowHandle
			, GPUGraph* initializeGraph
		);
		void Release();
		void UpdateIMGUI();
		void PrepareDrawData(GPUGraph* pRenderGraph);
		// captureTarget: when valid, the same IMGUI pass is also rendered into this external offscreen RT
		// so a test harness can Readback it (the presented backbuffer has no eTransferSrc usage). Default
		// (invalid) keeps the original single-target behavior.
		void Draw(GPUGraph* pRenderGraph, ImageHandle captureTarget = ImageHandle());
		void CustomTexture(ImVec2 const& offset, ImVec2 const& size, int id = 0);
		void DrawView(int id = 0);
		CRenderBackend* GetRenderBackend() const { return p_RenderBackend; }
		IWindowSystem* GetWindowSystem() const { return p_WindowSystem; }
		castl::vector<castl::shared_ptr<graphics_backend::WindowHandle>> const& GetWindowHandles() const {
			return m_WindowHandles;
		}
		castl::deque<IMGUITextureViewContext> const& GetTextureViewContexts() const {
			return m_TextureViewContexts;
		}
	public:
		void PrepareSingleViewGUIResources(ImGuiViewport* viewPort, GPUGraph* renderGraph);
		void DrawSingleView(ImGuiViewport* viewPort, GPUGraph* renderGraph, ImageHandle captureTarget);
		void PrepareInitViewportContext(ImGuiViewport* viewPort, castl::shared_ptr<IWindow> const& pWindow, bool mainWindow = false);
		void ReleaseViewportContext(ImGuiViewport* viewPort);

		IWindow* m_MouseWindow = nullptr;
		ImVec2 m_LastValidMousePos = ImVec2(-FLT_MAX, -FLT_MAX);
	private:
		void NewFrame();
		castl::shared_ptr<GPUTexture> m_Fontimage;
		ShaderConstantsBuilder m_ImguiShaderConstantsBuilder;
		ShaderBindingBuilder m_ImguiShaderBindingBuilder;
		castl::vector<castl::shared_ptr<graphics_backend::WindowHandle>> m_WindowHandles;
		threadsafe_utils::TThreadSafePointerPool<IMGUIViewportContext> m_ViewportContextPool;
		castl::deque<IMGUITextureViewContext> m_TextureViewContexts;
		IWindowSystem* p_WindowSystem;
		CRenderBackend* p_RenderBackend;
	};
}