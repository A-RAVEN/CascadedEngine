#pragma once
#include <memory>
#include <CRenderBackend.h>
#include <ShaderProvider.h>
#include <GPUTexture.h>
#include <CAResource/ResourceManagingSystem.h>
#include <ThreadSafePool.h>
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
	};
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
	void UpdateIMGUI(graphics_backend::WindowHandle const* windowHandle);
	void NewFrame();
	void DrawIMGUI(
		graphics_backend::CRenderGraph* renderGraph
		, graphics_backend::TextureHandle renderTargethandle);
	graphics_backend::CRenderBackend* GetRenderBackend() const { return p_RenderBackend; }
public:
	void PrepareSingleViewGUIResources(ImGuiViewport* viewPort, graphics_backend::CRenderGraph* renderGraph);
	void PrepareInitViewportContext(ImGuiViewport* viewPort, graphics_backend::WindowHandle* pWindow);
	void ReleaseViewportContext(ImGuiViewport* viewPort);
private:
	castl::shared_ptr<graphics_backend::GPUTexture> m_Fontimage;
	castl::shared_ptr<graphics_backend::TextureSampler> m_ImageSampler;
	GraphicsShaderSet m_ImguiShaderSet;
	ShaderConstantsBuilder m_ImguiShaderConstantsBuilder;
	ShaderBindingBuilder m_ImguiShaderBindingBuilder;
	graphics_backend::CRenderBackend* p_RenderBackend;
	threadsafe_utils::TThreadSafePointerPool<IMGUIViewportContext> m_ViewportContextPool;
};