#pragma once
#include <memory>
#include "IMGUIIncludes.h"
#include <CRenderBackend.h>
#include <ShaderProvider.h>
#include <GPUTexture.h>
#include <CAResource/ResourceManagingSystem.h>
#include <ThreadSafePool.h>
#include <glm/glm.hpp>
#include <CAUtils.h>
#include <CAWindow/WindowSystem.h>

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
			m_ShaderArgs = nullptr;
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
		castl::shared_ptr<ShaderArgList> m_ShaderArgs = nullptr;
		castl::vector<castl::tuple<uint32_t, uint32_t, uint32_t>> m_IndexDataOffsets;
		castl::vector<glm::uvec4> m_Sissors;
		castl::vector<castl::shared_ptr<ShaderArgList>> m_TextureBindings;
		bool m_Draw = false;
	};

	struct IMGUITextureViewContext
	{
		ImageHandle m_RenderTarget;
		cacore::Rect<float> m_ViewportRect;
		GPUTextureDescriptor m_TextureDescriptor;
		int m_SceneViewIndex;
	};


	class IMGUIContext
	{
	public:
		IMGUIContext();
		void Initialize(castl::string const& editorConfigPath
			, castl::shared_ptr<CRenderBackend> const& renderBackend
			, castl::shared_ptr<IWindowSystem> const& windowSystem
			, castl::shared_ptr<IWindow> const& mainWindowHandle
			, resource_management::ResourceManagingSystem* resourceSystem
			, GPUGraph* initializeGraph
		);
		void Release();
		void UpdateIMGUI();
		void PrepareDrawData(GPUGraph* pRenderGraph);
		void Draw(GPUGraph* pRenderGraph);
		void DrawView(int id = 0);
		castl::shared_ptr<CRenderBackend> GetRenderBackend() const { return p_RenderBackend; }
		castl::shared_ptr<IWindowSystem> GetWindowSystem() const { return p_WindowSystem; }
		castl::vector<castl::shared_ptr<graphics_backend::WindowHandle>> const& GetWindowHandles() const {
			return m_WindowHandles;
		}
		castl::deque<IMGUITextureViewContext> const& GetTextureViewContexts() const {
			return m_TextureViewContexts;
		}
	public:
		void PrepareSingleViewGUIResources(uint32_t& inoutHandleID, ImGuiViewport* viewPort, GPUGraph* renderGraph);
		void DrawSingleView(ImGuiViewport* viewPort, GPUGraph* renderGraph);
		void PrepareInitViewportContext(ImGuiViewport* viewPort, castl::shared_ptr<IWindow> const& pWindow, bool mainWindow = false);
		void ReleaseViewportContext(ImGuiViewport* viewPort);

		IWindow* m_MouseWindow = nullptr;
		ImVec2 m_LastValidMousePos = ImVec2(-FLT_MAX, -FLT_MAX);
	private:
		void NewFrame();
		castl::shared_ptr<GPUTexture> m_Fontimage;
		ShaderConstantsBuilder m_ImguiShaderConstantsBuilder;
		ShaderBindingBuilder m_ImguiShaderBindingBuilder;
		castl::shared_ptr<IWindowSystem> p_WindowSystem;
		castl::vector<castl::shared_ptr<graphics_backend::WindowHandle>> m_WindowHandles;
		castl::shared_ptr<CRenderBackend> p_RenderBackend;
		threadsafe_utils::TThreadSafePointerPool<IMGUIViewportContext> m_ViewportContextPool;
		castl::deque<IMGUITextureViewContext> m_TextureViewContexts;
		IShaderSet* m_ImguiShaderSet = nullptr;
	};
}