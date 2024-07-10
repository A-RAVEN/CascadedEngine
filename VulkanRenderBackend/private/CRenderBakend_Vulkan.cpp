#include "pch.h"
#include "CRenderBackend_Vulkan.h"
#include "WindowContext.h"
#include <CATimer/Timer.h>

namespace graphics_backend
{
	void CRenderBackend_Vulkan::Initialize(catimer::TimerSystem* timer, castl::string const& appName, castl::string const& engineName)
	{
		catimer::SetGlobalTimerSystem(timer);
		m_Application.InitApp(appName, engineName);
	}

	void CRenderBackend_Vulkan::InitializeThreadContextCount(uint32_t threadCount)
	{
	}
	void CRenderBackend_Vulkan::ScheduleGPUFrame(CTaskGraph* taskGraph, GPUFrame const& gpuFrame)
	{
		m_Application.ScheduleGPUFrame(taskGraph, gpuFrame);
	}
	castl::shared_ptr<GPUBuffer> CRenderBackend_Vulkan::CreateGPUBuffer(GPUBufferDescriptor const& descriptor)
	{
		return castl::shared_ptr<GPUBuffer>(m_Application.NewGPUBuffer(descriptor), [this](GPUBuffer* releaseBuffer)
			{
				m_Application.ReleaseGPUBuffer(releaseBuffer);
			});
	}
	void CRenderBackend_Vulkan::Release()
	{
		m_Application.ReleaseApp();
	}
	castl::shared_ptr<WindowHandle> CRenderBackend_Vulkan::GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window)
	{
		return m_Application.GetWindowHandle(window);
	}

	bool CRenderBackend_Vulkan::AnyWindowRunning()
	{
		return m_Application.AnyWindowRunning();
	}

	castl::shared_ptr<GPUTexture> CRenderBackend_Vulkan::CreateGPUTexture(GPUTextureDescriptor const& inDescriptor)
	{
		return castl::shared_ptr<GPUTexture>(m_Application.NewGPUTexture(inDescriptor)
			, [this](GPUTexture* releaseTex)
			{
				m_Application.ReleaseGPUTexture(releaseTex);
			});
	}
}
