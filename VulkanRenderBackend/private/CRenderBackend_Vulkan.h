#pragma once
#include <CRenderBackend.h>
#include <ThreadManager.h>
#include <ShaderBindingBuilder.h>
#include "VulkanApplication.h"

namespace graphics_backend
{
	class CRenderBackend_Vulkan : public CRenderBackend
	{
	public:
		void Initialize(castl::string const& appName, castl::string const& engineName) override;
		void InitializeThreadContextCount(uint32_t threadCount) override;
		void SetupGraphicsTaskGraph(thread_management::CTaskGraph* taskGraph, castl::vector<castl::shared_ptr<CRenderGraph>> const& pendingRenderGraphs, FrameType frameID) override;
		void Release() override;
		castl::shared_ptr<WindowHandle> NewWindow(uint32_t width, uint32_t height, castl::string const& windowName
			, bool visible
			, bool focused
			, bool decorate
			, bool floating) override;
		bool AnyWindowRunning() override;
		void TickWindows() override;
		virtual void PushRenderGraph(castl::shared_ptr<CRenderGraph> inRenderGraph) override;
		virtual castl::shared_ptr<GPUBuffer> CreateGPUBuffer(EBufferUsageFlags usageFlags
			, uint64_t count
			, uint64_t stride) override;
		virtual castl::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& inDescriptor) override;
		virtual castl::shared_ptr<ShaderConstantSet> CreateShaderConstantSet(ShaderConstantsBuilder const& inBuilder) override;
		virtual castl::shared_ptr<ShaderBindingSet> CreateShaderBindingSet(ShaderBindingBuilder const& inBuilder) override;
		virtual castl::shared_ptr<TextureSampler> GetOrCreateTextureSampler(TextureSamplerDescriptor const& descriptor) override;
		virtual uint32_t GetMonitorCount() const override;
		virtual MonitorHandle GetMonitorHandleAt(uint32_t monitorID) const override;
	private:
		CVulkanApplication m_Application;
	};
}
