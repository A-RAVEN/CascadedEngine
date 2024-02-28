#pragma once
#include <memory>
#include <CRenderBackend.h>
#include <ShaderProvider.h>
#include <GPUTexture.h>
#include <CAResource/ResourceManagingSystem.h>

class IMGUIContext
{
public:
	IMGUIContext();
	void Initialize(
		graphics_backend::CRenderBackend* renderBackend
		, resource_management::ResourceManagingSystem* resourceSystem
	);
	void Release();
	void UpdateIMGUI(graphics_backend::WindowHandle const* windowHandle);
	void NewFrame();
	void DrawIMGUI(
		graphics_backend::CRenderGraph* renderGraph
		, graphics_backend::TextureHandle renderTargethandle);
	graphics_backend::CRenderBackend* GetRenderBackend() const { return p_RenderBackend; }
private:
	castl::shared_ptr<graphics_backend::GPUTexture> m_Fontimage;
	castl::shared_ptr<graphics_backend::TextureSampler> m_ImageSampler;
	GraphicsShaderSet m_ImguiShaderSet;
	ShaderConstantsBuilder m_ImguiShaderConstantsBuilder;
	ShaderBindingBuilder m_ImguiShaderBindingBuilder;
	graphics_backend::CRenderBackend* p_RenderBackend;
};