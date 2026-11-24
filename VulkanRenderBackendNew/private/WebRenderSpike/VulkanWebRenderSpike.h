#pragma once
// Vulkan 共享图像助手（editor-web-render-spike 5.2）：**非绑定面**，不进 CODEGEN_ANNOTATED。
//
// 机制（design D6）：
//   - 共享图像 = `VK_KHR_external_memory_win32` 的 Win32 NT 句柄
//     （`VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT`）：图像创建带 `VkExternalMemoryImageCreateInfo`，
//     分配带 `VkExportMemoryAllocateInfo`，用 `vkGetMemoryWin32HandleKHR` 取 HANDLE。
//   - 第二消费者：把同一个 HANDLE 用 `VkImportMemoryWin32HandleInfoKHR` 导入成**独立的 VkImage/VkDeviceMemory**
//     （规范明确允许"导入回导出它的同一个实例"，见 evidence/5.1 的引文），再读回像素比对。
//   - 渲染：本类自己建一个 VkDevice（**同一物理设备**，只启用 external memory 扩展），用嵌入的 SPIR-V
//     （SpikeShaders.inc）画与 D3D12 完全相同的 256×256 特征图案；提交后按帧读回。
//     为什么不用后端那个 VkDevice：`RenderBackend_Vulkan` 创建设备时没有启用 `VK_KHR_external_memory_win32`
//     （只启用 swapchain/GPL），而本 change 的规范禁止改后端公共代码；所以助手在同一物理设备上另建设备，
//     引擎后端的设备/队列/分配路径一行不动。
//   - 读回走 `vkCmdCopyImageToBuffer` → map → 紧凑拷贝（Vulkan 读回惯例），像素经同一 C++→C#→网页通道。
//
// 设备不支持 `VK_KHR_external_memory_win32` 时：`Create` 明确记录（LastError + 日志）并退化成**不导出句柄**
// 的 CPU 读回（`HasSharedHandle()` 为假，`ReadbackViaOpenedHandle` 明确报"设备不支持"），不静默。
#include <WebRenderSpike/IWebRenderSpike.h>
#include <Utils/VulkanIncludes.h>
#include <memory>
#include <string>

namespace graphics_backend
{
	class RenderBackend_Vulkan;

	class VulkanWebRenderSpikeTexture final : public IWebRenderSpikeSharedTexture
	{
	public:
		explicit VulkanWebRenderSpikeTexture(RenderBackend_Vulkan* backend);
		~VulkanWebRenderSpikeTexture();

		bool Create(uint32_t width, uint32_t height) override;
		bool RenderFrame(uint32_t frameIndex) override;
		bool Readback(uint8_t* dst, uint64_t capacity) override;
		bool ReadbackViaOpenedHandle(uint8_t* dst, uint64_t capacity) override;
		bool HasSharedHandle() const override { return m_SharedHandle != nullptr; }
		uint32_t Width() const override { return m_Width; }
		uint32_t Height() const override { return m_Height; }
		char const* LastError() const override { return m_LastError.c_str(); }

		/** 释放设备/图像/管线/句柄（幂等；provider 析构时调用）。 */
		void Release();

		/** 设备是否支持 Win32 外部内存导出（不支持时已明确记录并退 CPU 读回）。 */
		bool ExternalMemorySupported() const { return m_ExternalMemorySupported; }

	private:
		bool Fail(std::string message);
		bool CreateDevice();
		bool CreateImage();
		bool CreatePipeline();
		bool EnsureImportedImage();
		bool SubmitAndWait(vk::CommandBuffer commandBuffer);
		bool CopyImageToHost(vk::Image image, bool importedImage, uint8_t* dst, uint64_t capacity,
		                     char const* what);
		void ReleaseResources();
		uint32_t FindMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags required) const;

		RenderBackend_Vulkan* m_Backend = nullptr;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		std::string m_LastError;
		std::string m_DeviceName;

		bool m_ExternalMemorySupported = false;
		// 是否至少渲染过一帧：没渲染过时图像仍是 UNDEFINED 布局，读回前必须先做 UNDEFINED→GENERAL 过渡
		//（`skip-native-render` 红态会走这条路径：不渲染直接读回，判据应在像素上红、而不是撞布局校验）。
		bool m_HasRendered = false;
		HANDLE m_SharedHandle = nullptr;

		vk::PhysicalDevice m_PhysicalDevice;
		vk::Device m_Device;
		vk::Queue m_Queue;
		uint32_t m_QueueFamily = 0;
		vk::CommandPool m_CommandPool;
		vk::CommandBuffer m_CommandBuffer;
		vk::Fence m_Fence;

		vk::Image m_Image;
		vk::DeviceMemory m_Memory;
		// 生产者分配的尺寸与内存类型：导入同一个 payload 必须用与导出方兼容的内存类型与同尺寸
		//（不能拿导入图像自己算出来的 index 重新挑一个，可能挑到别的类型）。
		uint64_t m_MemorySize = 0;
		uint32_t m_MemoryTypeIndex = UINT32_MAX;
		vk::ImageView m_View;
		vk::RenderPass m_RenderPass;
		vk::Framebuffer m_Framebuffer;
		vk::PipelineLayout m_PipelineLayout;
		vk::Pipeline m_Pipeline;
		vk::ShaderModule m_VertexShader;
		vk::ShaderModule m_FragmentShader;

		// 第二消费者：导入同一句柄得到的独立 VkImage/VkDeviceMemory。
		vk::Image m_ImportedImage;
		vk::DeviceMemory m_ImportedMemory;
	};

	/**
	 * 非绑定面接口的适配器（照 D3D12 的 `D3D12WebRenderSpikeProvider` 同样的形态）：
	 * 不参与被 CodeGen 解析的头（`RenderBackend_Vulkan.h` 保持原样），由 .cpp 在 `Init` 里建好并以实例名
	 * "WebRenderSpike" 登记进模块管理器；测试按名取到它。两个后端模块同一时刻只加载一个，实例名不冲突。
	 */
	class VulkanWebRenderSpikeProvider final : public IWebRenderSpikeProvider
	{
	public:
		explicit VulkanWebRenderSpikeProvider(RenderBackend_Vulkan* backend);
		~VulkanWebRenderSpikeProvider();

		IWebRenderSpikeSharedTexture* GetWebRenderSpikeTexture() override;

	private:
		RenderBackend_Vulkan* m_Backend = nullptr;
		std::unique_ptr<VulkanWebRenderSpikeTexture> m_Texture;
	};
}
