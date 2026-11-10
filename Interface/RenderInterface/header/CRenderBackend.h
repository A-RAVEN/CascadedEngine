#pragma once
// 绑定标注宏（CA_BIND / CA_BIND_SKIP / CA_BIND_INJECT）：csharp-native-direct-binding 2.2 起
// 这个头自己也进绑定面，所以标注宏必须在本头可见，不能靠别的头恰好带进来。
#include <CodeGenMeta.h>
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
// 两个元数据查询的入参用到 `cacore::PathHash`（`NameHash` 由 Common.h 带入）。
#include <HashKeys.h>
// add-render-readback: cross-backend GPU→host readback
#include <span>
#include <memory>
#include "ShaderResourceHandle.h"
// vulkan-gpu-queries：逐 pass GPU 计时的公共类型与结果对象（能力 `gpu-graph-timing`）。
#include "GPUTiming.h"

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

	// 原生渲染后端进绑定面（csharp-native-direct-binding 2.2 / design D1）：无参 CA_BIND 只声明绑定意图，
	// 编号由锁文件治理。四个 SKIP 是主循环裁定过的（各带理由，审计表记"不可绑 + 本轮未实现/④"）。
	CA_BIND()
	class CRenderBackend
	{
	public:
		virtual void ExecuteGraph(TaskScheduler* scheduler, castl::shared_ptr<GPUGraph> const& graph) = 0;
		// vulkan-gpu-queries（能力 `gpu-graph-timing`）：每队列的计时能力，以及一次计时图执行。
		// 普通 `ExecuteGraph` 保持原样，不承担查询开销（design D6）；只有 `ExecuteGraphTimed`
		// 才创建查询资源、记录时间戳并等待回读。
		// `GetTimingCapabilities(queue).passTimestamps` 为假时调用方不应发起计时；图实际使用的队列
		// 不支持计时时 `ExecuteGraphTimed` 返回空结果并记录原因，不返回全零的伪造结果（design D8）。
		//
		// 这两个入口**不是纯虚**：`CRenderBackend` 的纯虚方法集合被
		// `openspec/changes/archive/2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md`
		// 的可绑定面审计表按条数对账（`Tools/CodeGen/check_bindable_audit.py`，构建时由 CodeGen 调用）。
		// 那份审计表属于另一个 change，本 change 不改它的工件；因此这里给基类默认实现：能力报假、
		// 计时入口返回空并记录原因 —— 这正是 design D8 要求的"不支持就显式失败"，不是伪造成功。
		// 两个后端各自 override 出真实实现（D3D12 见 2.1，Vulkan 见 3.1）。
		virtual GPUTimingCapabilities GetTimingCapabilities(EGPUQueueType queue) const
		{
			(void)queue;
			return GPUTimingCapabilities{};
		}
		// 结果对象持有本次计时执行的回读数据与同步资源，生命周期独立于执行器和会被复用的帧上下文
		// （design D5）。`Wait()` 返回之后读 `Passes()`；不支持或时间无效时返回空指针。
		virtual std::unique_ptr<IGPUGraphTiming> ExecuteGraphTimed(
			TaskScheduler* scheduler,
			castl::shared_ptr<GPUGraph> const& graph)
		{
			(void)scheduler;
			(void)graph;
			CA_LOG_ERR("当前后端不支持计时：这个后端没有实现 GetTimingCapabilities / ExecuteGraphTimed");
			return nullptr;
		}
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
		// csharp-render-shader-constants 任务 1.1：按类型名 / 按着色器路径查询结构元数据。
		// 适配层用它们在做任何后端调用之前完成校验（`CreateShaderStruct` 对未知类型会解引用空元数据，
		// 见 `D3D12RenderBackend/private/ShaderLibrary/D3D12ShaderStruct.cpp` 构造函数体内第一处实际使用）。
		// 两个查询的契约：**未命中返回空指针且不打断**。两个后端 MUST 直读各自 `ShaderLibrary` 公开段的
		// `m_ShaderStructs` / `m_ShaderRootStructs`；不得复用 Vulkan 现有 getter，它未命中先执行
		// `CA_LOG_ERR_BREAK`（`__debugbreak`，Release 不消除）。
		virtual ShaderCompilerSlang::ShaderStructData const* FindShaderStructData(
			cacore::NameHash const& structType) = 0;
		virtual ShaderCompilerSlang::ShaderStructData const* FindShaderRootStructData(
			cacore::PathHash const& shaderPath) = 0;
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
	// 参数位置裸指针的唯一通道（design D1 / csharp-native-direct-binding 2.2）：调度器由引擎钩子提供，
	// 不进 C ABI、不进托管表面。钩子名固定 `<类>_<方法>_<参数>`，定义在宿主（CSharpHost）。
	CA_BIND_INJECT(ExecuteGraph, scheduler)
	// 四条按主循环裁定排除（审计表逐行记"不可绑 + 原因"）。
	CA_BIND_SKIP(ExecuteGraphTimed, "本轮未实现：返回 unique_ptr<IGPUGraphTiming>，要让这条进表必须把第五个接口一起放门；脚本渲染用例不用计时")
	CA_BIND_SKIP(RunTestCode, "④ 语义越界：测试专用入口，脚本不该调")
	CA_BIND_SKIP(Release, "④ 语义越界：销毁整个后端，脚本不该调（沿用 csharp-bindable-surface 审计表既定结论）")
	CA_BIND_SKIP(Readback, "本轮未实现：std::span 是 design Non-Goals 点名的 GPU 查询批形状（另立批）")
	// 7.1（用户 2026-11-10 裁定）：`CreateGPUBuffer` 不再排除。`GPUBuffer` 已挂 CA_BIND（第六原生接口），
	// 返回的 `shared_ptr<GPUBuffer>` 走"已绑定接口按对象登记"——托管侧拿表面对象、`Dispose` 只放托管引用，
	// 原生对象由 `shared_ptr` 计数保活；缓冲生命周期跟随后端，跨图复用不需要重分配。
	// `GetWindowHandle` 从 2.4 起进表（design D12）：`shared_ptr<IWindow>` 是已绑定接口按句柄、
	// `shared_ptr<WindowHandle>` 是已绑定接口按对象登记 —— 全是既有机制，不再排除。
	// 注入钩子声明：生成器在头文本里核对它存在，定义在宿主侧。
	extern "C" void* CAInject__graphics_backend__CRenderBackend__ExecuteGraph__scheduler();
}



