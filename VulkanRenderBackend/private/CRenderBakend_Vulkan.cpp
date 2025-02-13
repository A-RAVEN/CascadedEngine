#include "pch.h"
#include <CACore/CAHash.h>
#include "CRenderBackend_Vulkan.h"
#include "WindowContext.h"
#include <CATimer/Timer.h>

namespace graphics_backend
{
	void CRenderBackend_Vulkan::Initialize(catimer::TimerSystem* timer
		, ca_io::IOManager* ioManager
		, resource_management::ResourceManagingSystem* resourceManager
		, resource_management::ResourceImportingSystem* resourceImporter
		, castl::string const& appName
		, castl::string const& engineName)
	{
		catimer::SetGlobalTimerSystem(timer);
		m_Application.InitApp(appName, engineName, resourceManager);
		resourceImporter->AddImporter(&m_Application.m_ShaderResourceImporter);
	}

	void CRenderBackend_Vulkan::ScheduleGPUFrame(TaskScheduler* scheduler, GPUFrame const& gpuFrame)
	{
		m_Application.ScheduleGPUFrame(scheduler, gpuFrame);
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

	castl::shared_ptr<ShaderStruct> CRenderBackend_Vulkan::CreateShaderStruct(cacore::NameHash const& structType)
	{
		return m_Application.CreateShaderStruct(structType);
	}

	void CRenderBackend_Vulkan::RunTestCode()
	{
		castl::cout << cacore::has_std_hash<vk::RenderPassCreateInfo> << castl::endl;
	}
}
