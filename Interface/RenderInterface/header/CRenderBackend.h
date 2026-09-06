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
#include <Compiler.h>
// add-render-readback: cross-backend GPU→host readback
#include <span>
#include <memory>
#include "ShaderResourceHandle.h"

namespace thread_management
{
	class CThreadManager;
	class CTaskGraph;
	class TaskScheduler;
}

namespace graphics_backend
{
	using namespace thread_management;

	// GPU→host readback token (add-render-readback). Readback() declares a copy of a GPU resource
	// into the caller's CPU span; Wait() is the ONLY sync point, after which dst is filled.
	// The carrier is FIXED and documented: image → GetImageByteSize(width,height,format) tightly-packed
	// bytes (compact, no pitch padding; e.g. width*height*4 for E_R8G8B8A8_UNORM); buffer →
	// descriptor.SizeInByte() raw bytes. GetImageByteSize (Common.h) is the SOLE public carrier-size
	// source — callers SHALL NOT derive it as width*height*GetFormatBlockSize(format) (that per-pixel
	// block size is an internal intermediate for compact row strides; its `default=4` silently invents
	// a size for unknown formats). Caller derives width/height/format from the resource's own descriptor
	// (GPUTexture::GetDescriptor / GPUBuffer::GetDescriptor, queryable ANYTIME). The token carries NO
	// resource metadata — only the sync/ownership handle (Wait/IsReady/Reset). The concrete backend owns
	// the staging/sync it allocates and frees it in Reset()/dtor (idempotent). Caller owns dst and MUST
	// keep the memory it refers to alive AND its size unchanged until Wait() returns — do not resize or
	// release the backing container before Wait() (the backend rejects a shrunken dst rather than over/under-write).
	class IReadbackToken
	{
	public:
		virtual ~IReadbackToken() = default;
		virtual void Wait() = 0;          // block until GPU has written resource bytes into dst
		virtual bool IsReady() const = 0; // non-blocking query (true once Wait() would be a no-op)
		virtual void Reset() = 0;         // explicit release of internal staging/sync (idempotent)
	};

	class CRenderBackend
	{
	public:
		virtual void ExecuteGraph(TaskScheduler* scheduler, castl::shared_ptr<GPUGraph> const& graph) = 0;
		virtual void Release() = 0;
		virtual castl::shared_ptr<GPUBuffer> CreateGPUBuffer(GPUBufferDescriptor const& descriptor, EBufferUsageFlags usageFlags) = 0;
		castl::shared_ptr<GPUBuffer> CreateGPUBuffer(EBufferUsageFlags usageFlags
			, uint64_t count
			, uint64_t stride)
		{
			return CreateGPUBuffer(GPUBufferDescriptor::Create(count, stride), usageFlags);
		}
		virtual castl::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& inDescriptor, ETextureAccessTypeFlags accessType) = 0;
		virtual castl::shared_ptr<ShaderStruct> CreateShaderStruct(cacore::NameHash const& structType) = 0;
		virtual castl::shared_ptr<WindowHandle> GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window) = 0;
		virtual bool AnyWindowRunning() = 0;
		virtual void WaitIdle() = 0;
		virtual void RunTestCode(){};

		// add-render-readback: cross-backend GPU→host readback. Read the given resource's bytes
		// into the caller's span. For an image, dst must be >= GetImageByteSize(width,height,format);
		// for a buffer, >= descriptor.SizeInByte(). The backend checks dst.size() before writing and
		// rejects out-of-bounds. The caller keeps the memory dst refers to alive, and its size
		// unchanged, until Wait() returns (must not resize/release the backing container before Wait).
		// Implementation is backend-specific (Vulkan vkCmdCopyImageToBuffer/vkCmdCopyBuffer + staging +
		// fence; D3D12 CopyTextureRegion/CopyBufferRegion + READBACK + fence). Only single-mip /
		// single-layer / non-MSAA / 2D external images and external buffers are supported; other images
		// and unsupported formats are rejected with a null token.
		virtual std::unique_ptr<IReadbackToken> Readback(ImageHandle const& image, std::span<uint8_t> dst) = 0;
		virtual std::unique_ptr<IReadbackToken> Readback(BufferHandle const& buffer, std::span<uint8_t> dst) = 0;
	};
}



