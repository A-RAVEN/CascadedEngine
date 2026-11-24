#pragma once
// 渲染原型的**非绑定面**原生接口（editor-web-render-spike 2.1）。
//
// 为什么单独一个头：`GPUTexture` 在可绑定面上（interface-ids.lock 编号 6），给它加"共享句柄"
// 方法会重生成绑定产物、与「绑定产物逐字节不变」互斥。所以 D3D12 共享纹理走**独立原生助手**：
// 本头**没有 CA_BIND 标注、也不进 build.py 的 CODEGEN_ANNOTATED**，CodeGen 完全看不到它，
// 生成的 C#/C++ 绑定一行都不变。
//
// 谁实现：`graphics_backend::RenderBackend_D3D12`（D3D12 模块，渲染设备/队列都在它手里）。
// 谁使用：`Test/GPUBackendTester` 的 `TestWebRenderSpike`（经模块管理器按实例名 "WebRenderSpike" 取）。
// 跨 DLL 安全：接口只有纯虚函数 + POD 参数（`uint8_t*` / `uint32_t` / `char const*`），
// 没有 STL 类型过界，两边用同一份头编译，vtable 布局一致。
#include <cstdint>

namespace graphics_backend
{
	/**
	 * D3D12 共享纹理助手（实现见 `D3D12RenderBackend/private/WebRenderSpike/D3D12WebRenderSpike.cpp`）。
	 *
	 * 生命周期：由后端的 `GetWebRenderSpikeTexture()` 懒创建、由后端 `Release()` 释放；
	 * 调用方不 delete。所有方法只在**引擎主线程**调用，且调用期间后端不提交其它图
	 *（spike 用例里后端本来就没有别的渲染工作）。
	 */
	class IWebRenderSpikeSharedTexture
	{
	public:
		/**
		 * 建固定尺寸 RGBA8 共享堆纹理：`D3D12_HEAP_FLAG_SHARED` + `ID3D12Device::CreateSharedHandle`。
		 * 重复调用返回同一对象的状态（幂等：尺寸相同就不再重建）。
		 */
		virtual bool Create(uint32_t width, uint32_t height) = 0;

		/** 渲染一帧特征图案（背景绿 + 中心红三角 + 四角标记 + 帧序号位）并等待 GPU 完成。 */
		virtual bool RenderFrame(uint32_t frameIndex) = 0;

		/** 本设备读回（共享纹理 → READBACK 堆 → 紧凑 RGBA8 字节）。`capacity` 必须 ≥ 宽×高×4。 */
		virtual bool Readback(uint8_t* dst, uint64_t capacity) = 0;

		/**
		 * 第二消费者读回：`ID3D12Device::OpenSharedHandle` 打开**同一个共享句柄**得到独立资源对象，
		 * 再从它读回像素。用来证明句柄真的可被第二个消费者使用（不是"自己读自己"）。
		 */
		virtual bool ReadbackViaOpenedHandle(uint8_t* dst, uint64_t capacity) = 0;

		/** 共享句柄是否非空（`CreateSharedHandle` 成功的判据）。 */
		virtual bool HasSharedHandle() const = 0;

		virtual uint32_t Width() const = 0;
		virtual uint32_t Height() const = 0;

		/** 最近一次失败的原因（UTF-8，指向内部存储，调用方不得释放）；成功时为空串。 */
		virtual char const* LastError() const = 0;

	protected:
		// 虚析构 + protected：实现由 D3D12 模块内部持有/销毁，跨 DLL 不允许 delete（CRT 可能不同）。
		virtual ~IWebRenderSpikeSharedTexture() = default;
	};

	/** 后端实现这个接口，测试经模块管理器按实例名取到它（不新增绑定面）。 */
	class IWebRenderSpikeProvider
	{
	public:
		/** 懒创建并返回共享纹理助手；后端未初始化（设备为空）时返回空指针。 */
		virtual IWebRenderSpikeSharedTexture* GetWebRenderSpikeTexture() = 0;

	protected:
		virtual ~IWebRenderSpikeProvider() = default;
	};
}
