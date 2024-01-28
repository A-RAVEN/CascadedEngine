#pragma once
#include "VulkanApplication.h"
#include <RenderInterface/header/CRenderBackend.h>
#include <RenderInterface/header/ShaderBindingBuilder.h>

namespace graphics_backend
{
	class CRenderBackend_Vulkan : public CRenderBackend
	{
	public:
		void Initialize(std::string const& appName, std::string const& engineName) override;
		void InitializeThreadContextCount(uint32_t threadCount) override;
		void SetupGraphicsTaskGraph(CTaskGraph* taskGraph, std::vector<std::shared_ptr<CRenderGraph>> const& pendingRenderGraphs, FrameType frameID) override;
		void Release() override;
		std::shared_ptr<WindowHandle> NewWindow(uint32_t width, uint32_t height, std::string const& windowName) override;
		bool AnyWindowRunning() override;
		void TickWindows() override;
		virtual void PushRenderGraph(std::shared_ptr<CRenderGraph> inRenderGraph) override;
		virtual std::shared_ptr<GPUBuffer> CreateGPUBuffer(EBufferUsageFlags usageFlags
			, uint64_t count
			, uint64_t stride) override;
		virtual std::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& inDescriptor) override;
		virtual std::shared_ptr<ShaderConstantSet> CreateShaderConstantSet(ShaderConstantsBuilder const& inBuilder) override;
		virtual std::shared_ptr<ShaderBindingSet> CreateShaderBindingSet(ShaderBindingBuilder const& inBuilder) override;
		virtual std::shared_ptr<TextureSampler> GetOrCreateTextureSampler(TextureSamplerDescriptor const& descriptor) override;
	private:
		CVulkanApplication m_Application;
	};
}
