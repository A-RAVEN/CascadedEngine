#pragma once
// 脚本专用窄渲染适配面（csharp-render-backend-triangle）。
//
// 这里只放**跨脚本边界**的三个纯虚接口：目标、图、后端。它们只使用已经归档的绑定形状
// （定宽标量、公开记录、文本/路径值、同步字节、接口智能指针），不暴露 `TaskScheduler*`、
// `CRenderBackend::Release`、具体建图类或原生资源地址。
//
// 建图与提交的失败一律走 `int32_t` 状态 + `GetLastError()` 文本：0 = 成功、非 0 = 拒绝；
// 成功调用先清空旧错误。这样错误不穿过原生异常边界，也不依赖生成物内部的错误槽。
#include <stdint.h>

#include <CASTL/CAString.h>
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAVector.h>

#include <HashKeys.h>
#include "CodeGenMeta.h"
#include "GPUBuffer.h"

// 窗口只以智能指针入参出现：真实窗口由 `IWindowSystem` 创建，句柄由窗口接口发行。
namespace cawindow
{
	class IWindow;
}

namespace graphics_backend
{
	CA_BIND()
	class IScriptRenderTarget
	{
	public:
		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		virtual bool IsBackbuffer() const = 0;
		virtual ~IScriptRenderTarget() = default;
	};

	CA_BIND()
	class IScriptRenderGraph
	{
	public:
		// 顶点字节在同步调用内复制进原生图的上传持有区；描述里的字节数必须与数组长度一致。
		virtual int32_t AddBuffer(cacore::NameHash const& name, GPUBufferDescriptor const& descriptor,
			castl::vector<uint8_t> const& data) = 0;
		// 首版只支持 POSITION / COLOR 两个 `float3` 属性；任一 `offset + 12` 越过步长即拒绝。
		virtual int32_t SetPositionColorStream(cacore::NameHash const& streamName, uint32_t stride,
			uint32_t positionOffset, uint32_t colorOffset) = 0;
		virtual int32_t AddTrianglePass(castl::shared_ptr<IScriptRenderTarget> const& target,
			cacore::PathHash const& shaderPath, cacore::NameHash const& streamName,
			cacore::NameHash const& bufferName, uint32_t vertexCount, float red, float green,
			float blue, float alpha) = 0;
		// 只接受后台缓冲目标；本切片不设呈现目标数量上限，示例只用一个窗口。
		virtual int32_t Present(castl::shared_ptr<IScriptRenderTarget> const& target) = 0;
		virtual uint32_t GetPassCount() const = 0;
		virtual castl::string GetLastError() const = 0;
		virtual ~IScriptRenderGraph() = default;
	};

	CA_BIND()
	class IScriptRenderBackend
	{
	public:
		virtual castl::shared_ptr<IScriptRenderTarget> GetWindowTarget(
			castl::shared_ptr<cawindow::IWindow> const& window) = 0;
		virtual castl::shared_ptr<IScriptRenderTarget> CreateCaptureTarget(uint32_t width,
			uint32_t height) = 0;
		virtual castl::shared_ptr<IScriptRenderGraph> CreateGraph() = 0;
		virtual int32_t SubmitGraph(castl::shared_ptr<IScriptRenderGraph> const& graph) = 0;
		virtual void WaitIdle() = 0;
		virtual castl::string GetLastError() const = 0;
		virtual ~IScriptRenderBackend() = default;
	};
}
