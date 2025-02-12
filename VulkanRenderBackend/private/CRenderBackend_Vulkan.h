#pragma once
#include <CRenderBackend.h>
#include <ThreadManager.h>
#include <ShaderBindingBuilder.h>
#include <CAWindow/WindowSystem.h>
#include "VulkanApplication.h"

namespace graphics_backend
{
	class CRenderBackend_Vulkan : public CRenderBackend
	{
	public:
		void Initialize(catimer::TimerSystem* timer
			, ca_io::IOManager* ioManager
			, resource_management::ResourceManagingSystem* resourceManager
			, resource_management::ResourceImportingSystem* resourceImporter
			, castl::string const& appName
			, castl::string const& engineName) override;
		void Release() override;
		castl::shared_ptr<WindowHandle> GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window) override;
		bool AnyWindowRunning() override;
		virtual void ScheduleGPUFrame(TaskScheduler* scheduler, GPUFrame const& gpuFrame) override;
		virtual castl::shared_ptr<GPUBuffer> CreateGPUBuffer(GPUBufferDescriptor const& descriptor) override;
		virtual castl::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& inDescriptor) override;
		virtual castl::shared_ptr<ShaderStruct> CreateShaderStruct(cacore::NameHash const& structType) override;
		virtual void RunTestCode() override;
	private:
		CVulkanApplication m_Application;
	};
}
