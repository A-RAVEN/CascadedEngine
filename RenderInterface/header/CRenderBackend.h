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
		virtual castl::shared_ptr<WindowHandle> NewWindow(uint32_t width, uint32_t height, castl::string const& windowName) = 0;
		virtual bool AnyWindowRunning() = 0;
		virtual void TickWindows() = 0;
		virtual void PushRenderGraph(castl::shared_ptr<CRenderGraph> inRenderGraph) = 0;
		virtual castl::shared_ptr<GPUBuffer> CreateGPUBuffer(EBufferUsageFlags usageFlags
			, uint64_t count
			, uint64_t stride) = 0;
		virtual castl::shared_ptr<ShaderConstantSet> CreateShaderConstantSet(ShaderConstantsBuilder const& inBuilder) = 0;
		virtual castl::shared_ptr<ShaderBindingSet> CreateShaderBindingSet(ShaderBindingBuilder const& inBuilder) = 0;
		virtual castl::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& inDescriptor) = 0;
		virtual castl::shared_ptr<TextureSampler> GetOrCreateTextureSampler(TextureSamplerDescriptor const& descriptor) = 0;
	};
}



