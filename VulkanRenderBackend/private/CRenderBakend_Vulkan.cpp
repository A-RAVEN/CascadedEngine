#include "pch.h"
#include "CRenderBackend_Vulkan.h"

namespace graphics_backend
{
	void CRenderBackend_Vulkan::Initialize(castl::string const& appName, castl::string const& engineName)
	{
		m_Application.InitApp(appName, engineName);
	}

	void CRenderBackend_Vulkan::InitializeThreadContextCount(uint32_t threadCount)
	{
		m_Application.InitializeThreadContext(threadCount);
	}

	void CRenderBackend_Vulkan::SetupGraphicsTaskGraph(CTaskGraph* taskGraph
		, castl::vector<castl::shared_ptr<CRenderGraph>> const& pendingRenderGraphs
		, FrameType frameID)
	{
		m_Application.ExecuteStates(taskGraph, pendingRenderGraphs, frameID);
	}

	void CRenderBackend_Vulkan::Release()
	{
		m_Application.ReleaseApp();
	}

	castl::shared_ptr<WindowHandle> CRenderBackend_Vulkan::NewWindow(uint32_t width, uint32_t height, castl::string const& windowName)
	{
		return m_Application.CreateWindowContext(windowName, width, height);
	}

	bool CRenderBackend_Vulkan::AnyWindowRunning()
	{
		return m_Application.AnyWindowRunning();
	}

	void CRenderBackend_Vulkan::TickWindows()
	{
		//m_Application.TickWindowContexts();
	}

	void CRenderBackend_Vulkan::PushRenderGraph(castl::shared_ptr<CRenderGraph> inRenderGraph)
	{
		m_Application.PushRenderGraph(inRenderGraph);
	}

	castl::shared_ptr<GPUBuffer> CRenderBackend_Vulkan::CreateGPUBuffer(EBufferUsageFlags usageFlags, uint64_t count, uint64_t stride)
	{
		return castl::shared_ptr<GPUBuffer>(m_Application.NewGPUBuffer(usageFlags
			, count
			, stride), [this](GPUBuffer* releaseBuffer)
			{
				m_Application.ReleaseGPUBuffer(releaseBuffer);
			});
	}
	castl::shared_ptr<GPUTexture> CRenderBackend_Vulkan::CreateGPUTexture(GPUTextureDescriptor const& inDescriptor)
	{
		return castl::shared_ptr<GPUTexture>(m_Application.NewGPUTexture(inDescriptor)
			, [this](GPUTexture* releaseTex)
			{
				m_Application.ReleaseGPUTexture(releaseTex);
			});
	}
	castl::shared_ptr<ShaderConstantSet> CRenderBackend_Vulkan::CreateShaderConstantSet(ShaderConstantsBuilder const& inBuilder)
	{
		return m_Application.NewShaderConstantSet(inBuilder);
	}
	castl::shared_ptr<ShaderBindingSet> CRenderBackend_Vulkan::CreateShaderBindingSet(ShaderBindingBuilder const& inBuilder)
	{
		return m_Application.NewShaderBindingSet(inBuilder);
	}
	castl::shared_ptr<TextureSampler> CRenderBackend_Vulkan::GetOrCreateTextureSampler(TextureSamplerDescriptor const& descriptor)
	{
		return m_Application.GetOrCreateTextureSampler(descriptor);
	}
}
