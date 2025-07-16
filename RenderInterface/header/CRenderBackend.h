#pragma once
#include <CASTL/CAVector.h>
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAString.h>
#include <CAWindow/WindowSystem.h>
#include <CATimer/Timer.h>
#include "Common.h"
#include "GPUBuffer.h"
#include "CNativeRenderPassInfo.h"
#include "WindowHandle.h"
#include "ShaderBindingBuilder.h"
#include "TextureSampler.h"
#include "MonitorHandle.h"
#include "GPUFrame.h"
#include <IOManager/IOManager.h>
#include <CAResource/ResourceManagingSystem.h>
#include <CAResource/ResourceImportingSystem.h>
#include <ShaderStruct.h>

namespace thread_management
{
	class CThreadManager;
	class CTaskGraph;
	class TaskScheduler;
}

namespace graphics_backend
{
	using namespace thread_management;
	class CRenderBackend
	{
	public:
		virtual void Initialize(
			catimer::TimerSystem* timer
			, ca_io::IOManager* ioManager
			, resource_management::ResourceManagingSystem* resourceManager
			, resource_management::ResourceImportingSystem* resourceImporter
			, castl::string const& appName
			, castl::string const& engineName) = 0;
		virtual void ScheduleGPUFrame(TaskScheduler* scheduler, GPUFrame const& gpuFrame) = 0;
		virtual void ExecuteGraph(TaskScheduler* scheduler, castl::shared_ptr<GPUGraph> const& graph) = 0;

		virtual void Release() = 0;

		virtual castl::shared_ptr<GPUBuffer> CreateGPUBuffer(GPUBufferDescriptor const& descriptor) = 0;
		castl::shared_ptr<GPUBuffer> CreateGPUBuffer(EBufferUsageFlags usageFlags
			, uint64_t count
			, uint64_t stride)
		{
			return CreateGPUBuffer(GPUBufferDescriptor::Create(usageFlags, count, stride));
		}
		virtual castl::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& inDescriptor) = 0;
		virtual castl::shared_ptr<ShaderStruct> CreateShaderStruct(cacore::NameHash const& structType) = 0;

		virtual castl::shared_ptr<WindowHandle> GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window) = 0;
		virtual bool AnyWindowRunning() = 0;

		virtual void RunTestCode(){};
	};
}



