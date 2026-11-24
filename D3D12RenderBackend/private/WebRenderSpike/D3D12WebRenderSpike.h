#pragma once
// D3D12 共享纹理助手（editor-web-render-spike 2.1）：**非绑定面**，不进 CODEGEN_ANNOTATED。
//
// 位置放在 D3D12RenderBackend 模块里的原因：渲染要复用引擎后端的 `ID3D12Device`（真实类
// `RenderBackend_D3D12`），共享句柄也必须由同一个设备创建。绑定面（GPUTexture / CRenderBackend）
// 一行不动 —— 本类只被同模块的 `RenderBackend_D3D12` 实例化，测试经非绑定接口拿它。
//
// 线程与队列约定：所有方法只在引擎主线程调用；渲染/拷贝提交在**本类自己的 direct 队列**上
//（不占用后端的图队列，避免和引擎的图提交/屏障模型相互干扰），每次提交后用自己的 fence 等到完成。
#include <WebRenderSpike/IWebRenderSpike.h>
#include "D3D12Includes.h"
#include <memory>
#include <string>
#include <vector>

namespace graphics_backend
{
	class RenderBackend_D3D12;

	class D3D12WebRenderSpikeTexture final : public IWebRenderSpikeSharedTexture
	{
	public:
		explicit D3D12WebRenderSpikeTexture(RenderBackend_D3D12* backend);
		~D3D12WebRenderSpikeTexture();

		bool Create(uint32_t width, uint32_t height) override;
		bool RenderFrame(uint32_t frameIndex) override;
		bool Readback(uint8_t* dst, uint64_t capacity) override;
		bool ReadbackViaOpenedHandle(uint8_t* dst, uint64_t capacity) override;
		bool HasSharedHandle() const override { return m_SharedHandle != nullptr; }
		uint32_t Width() const override { return m_Width; }
		uint32_t Height() const override { return m_Height; }
		char const* LastError() const override { return m_LastError.c_str(); }

		/** 释放共享纹理 / 句柄 / 管线 / 队列（幂等；后端 Release 时调用）。 */
		void Release();

	private:
		bool EnsurePipeline(ID3D12Device* device);
		bool SubmitAndWait();
		bool CopyToReadbackAndWait(ID3D12Resource* source, uint8_t* dst, uint64_t capacity,
		                           char const* what);
		bool Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
		bool Fail(std::string message);
		void ReleaseResources();

		RenderBackend_D3D12* m_Backend = nullptr;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		std::string m_LastError;

		HANDLE m_SharedHandle = nullptr;
		ComPtr<ID3D12Resource> m_Resource;
		ComPtr<ID3D12Resource> m_OpenedResource;
		ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
		D3D12_CPU_DESCRIPTOR_HANDLE m_RtvHandle{};

		// 本类自己的提交设施（direct 队列 + 一次性命令分配器/列表 + fence）。
		ComPtr<ID3D12CommandQueue> m_Queue;
		ComPtr<ID3D12CommandAllocator> m_Allocator;
		ComPtr<ID3D12GraphicsCommandList> m_CommandList;
		ComPtr<ID3D12Fence> m_Fence;
		HANDLE m_FenceEvent = nullptr;
		uint64_t m_FenceValue = 0;

		ComPtr<ID3D12RootSignature> m_RootSignature;
		ComPtr<ID3D12PipelineState> m_PipelineState;
	};

	/**
	 * 非绑定面接口的**适配器**（editor-web-render-spike 2.1 修订）：它不参与被 CodeGen 解析的头
	 * （`RenderBackend_D3D12.h` 保持原样、不 include 本头、不继承新基类），由 .cpp 在 `Init` 里建好并以
	 * 实例名 "WebRenderSpike" 登记进模块管理器；测试按名取到它，内部转发到**同一个**后端设备。
	 */
	class D3D12WebRenderSpikeProvider final : public IWebRenderSpikeProvider
	{
	public:
		explicit D3D12WebRenderSpikeProvider(RenderBackend_D3D12* backend);
		~D3D12WebRenderSpikeProvider();

		IWebRenderSpikeSharedTexture* GetWebRenderSpikeTexture() override;

	private:
		RenderBackend_D3D12* m_Backend = nullptr;
		std::unique_ptr<D3D12WebRenderSpikeTexture> m_Texture;
	};
}
