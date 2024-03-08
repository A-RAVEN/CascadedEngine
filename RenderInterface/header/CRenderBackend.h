#pragma once
#include <CASTL/CAVector.h>
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAString.h>
#include "Common.h"
#include "GPUBuffer.h"
#include "CNativeRenderPassInfo.h"
#include "WindowHandle.h"
#include "CRenderGraph.h"
#include "ShaderBindingBuilder.h"
#include "ShaderBindingSet.h"
#include "TextureSampler.h"
#include "MonitorHandle.h"

namespace thread_management
{
	class CThreadManager;
	class CTaskGraph;
}

namespace graphics_backend
{
	class CRenderBackend
	{
	public:
		virtual void Initialize(castl::string const& appName, castl::string const& engineName) = 0;
		virtual void InitializeThreadContextCount(uint32_t threadContextCount) = 0;
		virtual void SetupGraphicsTaskGraph(
			thread_management::CTaskGraph* taskGraph
			, castl::vector<castl::shared_ptr<CRenderGraph>> const& pendingRenderGraphs
			, uint64_t frameID) = 0;
		virtual void Release() = 0;
	
		virtual void TickWindows() = 0;
		virtual void PushRenderGraph(castl::shared_ptr<CRenderGraph> inRenderGraph) = 0;
		virtual castl::shared_ptr<GPUBuffer> CreateGPUBuffer(EBufferUsageFlags usageFlags
			, uint64_t count
			, uint64_t stride) = 0;
		virtual castl::shared_ptr<ShaderConstantSet> CreateShaderConstantSet(ShaderConstantsBuilder const& inBuilder) = 0;
		virtual castl::shared_ptr<ShaderBindingSet> CreateShaderBindingSet(ShaderBindingBuilder const& inBuilder) = 0;
		virtual castl::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& inDescriptor) = 0;
		virtual castl::shared_ptr<TextureSampler> GetOrCreateTextureSampler(TextureSamplerDescriptor const& descriptor) = 0;
		
		//TODO: Remove Me To A Dedicate Window Manager Library
		virtual castl::shared_ptr<WindowHandle> NewWindow(uint32_t width, uint32_t height, castl::string const& windowName
			, bool visible
			, bool focused
			, bool decorate
			, bool floating) = 0;
		virtual bool AnyWindowRunning() = 0;
		virtual uint32_t GetMonitorCount() const = 0;
		virtual MonitorHandle GetMonitorHandleAt(uint32_t monitorID) const = 0;
		virtual void SetWindowFocusCallback(castl::function<void(WindowHandle*, bool)> callback) = 0;
		virtual void SetCursorEnterCallback(castl::function<void(WindowHandle*, bool)> callback) = 0;
		virtual void SetCursorPosCallback(castl::function<void(WindowHandle*, float, float)> callback) = 0;
		virtual void SetMouseButtonCallback(castl::function<void(WindowHandle*, int, int, int)> callback) = 0;
		virtual void SetScrollCallback(castl::function<void(WindowHandle*, float, float)> callback) = 0;
		virtual void SetKeyCallback(castl::function<void(WindowHandle*, int, int, int, int)> callback) = 0;
		virtual void SetCharCallback(castl::function<void(WindowHandle*, uint32_t)> callback) = 0;
		virtual void SetWindowCloseCallback(castl::function<void(WindowHandle*)> callback) = 0;
		virtual void SetWindowPosCallback(castl::function<void(WindowHandle*, float, float)> callback) = 0;
		virtual void SetWindowSizeCallback(castl::function<void(WindowHandle*, float, float)> callback) = 0;
	};
}



