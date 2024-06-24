#pragma once
#include <CASTL/CAVector.h>
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAString.h>
#include <CAWindow/WindowSystem.h>
#include "Common.h"
#include "GPUBuffer.h"
#include "CNativeRenderPassInfo.h"
#include "WindowHandle.h"
#include "ShaderBindingBuilder.h"
#include "ShaderBindingSet.h"
#include "TextureSampler.h"
#include "MonitorHandle.h"
#include "GPUFrame.h"

namespace thread_management
{
	class CThreadManager;
	class CTaskGraph;
}

namespace graphics_backend
{
	using namespace thread_management;
	class CRenderBackend
	{
	public:
		virtual void Initialize(castl::string const& appName, castl::string const& engineName) = 0;
		virtual void InitializeThreadContextCount(uint32_t threadContextCount) = 0;
		virtual void ScheduleGPUFrame(CTaskGraph* taskGraph, GPUFrame const& gpuFrame) = 0;
		virtual void Release() = 0;

		virtual castl::shared_ptr<GPUBuffer> CreateGPUBuffer(GPUBufferDescriptor const& descriptor) = 0;
		castl::shared_ptr<GPUBuffer> CreateGPUBuffer(EBufferUsageFlags usageFlags
			, uint64_t count
			, uint64_t stride)
		{
			return CreateGPUBuffer(GPUBufferDescriptor::Create(usageFlags, count, stride));
		}
		virtual castl::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& inDescriptor) = 0;
		virtual castl::shared_ptr<WindowHandle> GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window) = 0;
		virtual bool AnyWindowRunning() = 0;
	};
}



