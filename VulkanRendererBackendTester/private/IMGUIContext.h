#pragma once
#include <memory>
#include <imgui.h>
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
	//struct ImGuiViewport;
	class IMGUIContext;
	class IMGUIViewportContext
	{
	public:
		void Initialize() {
			m_ShaderArgs = castl::make_shared<ShaderArgList>();
		};
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
			m_VertexBuffer = {};
			m_IndexBuffer = {};
			m_ShaderArgs = castl::make_shared<ShaderArgList>();
		}
	public:
		int IgnoreWindowPosEventFrame = -1;
		int IgnoreWindowSizeEventFrame = -1;
	public:
		friend class IMGUIContext;
		castl::vector<ImageHandle> m_ViewportTextureHandles;
		IMGUIContext* pContext;
		castl::shared_ptr<IWindow> pWindowHandle;

		BufferHandle m_VertexBuffer;
		BufferHandle m_IndexBuffer;
		castl::shared_ptr<ShaderArgList> m_ShaderArgs;
		//ShaderConstantSetHandle m_ShaderConstants;
		//ShaderBindingSetHandle m_ShaderBindings;
		castl::vector<castl::tuple<uint32_t, uint32_t, uint32_t>> m_IndexDataOffsets;
		castl::vector<glm::uvec4> m_Sissors;
		//castl::vector<ShaderBindingSetHandle> m_TextureBindings;
		bool m_Draw;
	};

	struct IMGUITextureViewContext
	{
		ImageHandle m_RenderTarget;
		cacore::Rect<float> m_ViewportRect;
		int m_SceneViewIndex;
	};


	class IMGUIContext
	{
	public:
		IMGUIContext();
		void Initialize(
			castl::shared_ptr<CRenderBackend> const& renderBackend
			, castl::shared_ptr<IWindowSystem> const& windowSystem
			, castl::shared_ptr<IWindow> const& mainWindowHandle
			, resource_management::ResourceManagingSystem* resourceSystem
		);
		void Release();
		void UpdateIMGUI();
		void PrepareDrawData(GPUGraph* pRenderGraph);
		void Draw(GPUGraph* pRenderGraph);
		void DrawIMGUI(
			GPUGraph* renderGraph
			, ImageHandle renderTargethandle);
		void DrawView(int id = 0);
		castl::shared_ptr<CRenderBackend> GetRenderBackend() const { return p_RenderBackend; }
		castl::shared_ptr<IWindowSystem> GetWindowSystem() const { return p_WindowSystem; }
	public:
		void PrepareSingleViewGUIResources(ImGuiViewport* viewPort, GPUGraph* renderGraph);
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
		castl::shared_ptr<CRenderBackend> p_RenderBackend;
		threadsafe_utils::TThreadSafePointerPool<IMGUIViewportContext> m_ViewportContextPool;
		castl::deque<IMGUITextureViewContext> m_TextureViewContexts;

		IShaderSet* m_ImguiShaderSet;
	};
}