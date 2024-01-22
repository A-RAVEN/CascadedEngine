#pragma once
#include <memory>
#include <RenderInterface/header/CRenderBackend.h>
#include <RenderInterface/header/ShaderProvider.h>
#include <RenderInterface/header/GPUTexture.h>
#include <GeneralResources/header/ResourceManagingSystem.h>

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
	void DrawIMGUI(
		graphics_backend::CRenderGraph* renderGraph
		, graphics_backend::TextureHandle renderTargethandle);
private:
	std::shared_ptr<graphics_backend::GPUTexture> m_Fontimage;
	std::shared_ptr<graphics_backend::TextureSampler> m_ImageSampler;
	GraphicsShaderSet m_ImguiShaderSet;
	ShaderConstantsBuilder m_ImguiShaderConstantsBuilder;
	ShaderBindingBuilder m_ImguiShaderBindingBuilder;
};