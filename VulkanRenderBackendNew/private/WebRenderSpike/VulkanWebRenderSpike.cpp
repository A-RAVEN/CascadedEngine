// Vulkan 共享图像助手实现（editor-web-render-spike 5.2）。头文件里有机制说明；这里是实现。
//
// 文档依据：
//   - 导出：https://docs.vulkan.org/refpages/latest/refpages/source/vkGetMemoryWin32HandleKHR.html
//   - 导入（同一实例内导入同一句柄是规范允许的）：
//     https://docs.vulkan.org/refpages/latest/refpages/source/VkImportMemoryWin32HandleInfoKHR.html
//
// 线程与队列：所有方法只在引擎主线程调用；提交走本类自己的 queue + fence（提交后立刻等待），
// 不占用后端图队列，也不碰 `VulkanMemoryManager` 的公共分配路径。
#include "WebRenderSpike/VulkanWebRenderSpike.h"
#include "RenderBackend_Vulkan.h"
#include "WebRenderSpike/SpikeShaders.inc"

#include <array>
#include <cstring>
#include <vector>
#include <windows.h>

namespace graphics_backend
{
	namespace
	{
		// 特征图案的固定版式（与 D3D12 的助手、网页采样表逐点一致）。
		constexpr float kFrameBitOriginX = 32.0f;
		constexpr float kFrameBitOriginY = 4.0f;
		constexpr float kFrameBitSize = 8.0f;
		constexpr uint32_t kPushConstantBytes = 64;

		bool HasDeviceExtension(std::vector<vk::ExtensionProperties> const& extensions, char const* name)
		{
			for (vk::ExtensionProperties const& extension : extensions)
			{
				if (std::strcmp(extension.extensionName, name) == 0)
				{
					return true;
				}
			}
			return false;
		}

		char const* ResultName(VkResult result)
		{
			switch (result)
			{
			case VK_SUCCESS: return "VK_SUCCESS";
			case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
			case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
			case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
			case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
			case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
			default: return "VK_ERROR_<其它>";
			}
		}
	}

	VulkanWebRenderSpikeTexture::VulkanWebRenderSpikeTexture(RenderBackend_Vulkan* backend)
		: m_Backend(backend)
	{
	}

	VulkanWebRenderSpikeTexture::~VulkanWebRenderSpikeTexture()
	{
		Release();
	}

	bool VulkanWebRenderSpikeTexture::Fail(std::string message)
	{
		m_LastError = std::move(message);
		return false;
	}

	bool VulkanWebRenderSpikeTexture::Create(uint32_t width, uint32_t height)
	{
		m_LastError.clear();
		if (m_Backend == nullptr)
		{
			return Fail("Vulkan 后端为空（共享图像助手拿不到物理设备）");
		}
		if (width == 0 || height == 0)
		{
			return Fail("纹理尺寸非法：" + std::to_string(width) + "x" + std::to_string(height));
		}
		if (m_Device && m_Width == width && m_Height == height)
		{
			return true;
		}
		ReleaseResources();
		m_Width = width;
		m_Height = height;
		if (!CreateDevice())
		{
			return false;
		}
		if (!CreateImage())
		{
			return false;
		}
		if (!CreatePipeline())
		{
			return false;
		}
		return true;
	}

	bool VulkanWebRenderSpikeTexture::CreateDevice()
	{
		m_PhysicalDevice = m_Backend->GetVulkanPhysicalDevice();
		if (!m_PhysicalDevice)
		{
			return Fail("Vulkan 物理设备为空（后端未初始化）");
		}
		try
		{
			vk::PhysicalDeviceProperties properties = m_PhysicalDevice.getProperties();
			m_DeviceName = std::string(properties.deviceName.data(), properties.deviceName.size());
			while (!m_DeviceName.empty() && m_DeviceName.back() == '\0')
			{
				m_DeviceName.pop_back();
			}

			// 设备扩展：export 的 Win32 句柄必须有 VK_KHR_external_memory_win32。
			std::vector<vk::ExtensionProperties> extensions = m_PhysicalDevice.enumerateDeviceExtensionProperties();
			const bool hasExternalMemory = HasDeviceExtension(extensions, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
			const bool hasExternalMemoryWin32 =
				HasDeviceExtension(extensions, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
			m_ExternalMemorySupported = hasExternalMemoryWin32;
			std::vector<char const*> enabledExtensions;
			if (hasExternalMemory)
			{
				enabledExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
			}
			if (hasExternalMemoryWin32)
			{
				enabledExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
			}
			CA_LOG("[渲染原型] Vulkan 设备：{}，externalMemory={} externalMemoryWin32={}", m_DeviceName,
			       hasExternalMemory ? 1 : 0, hasExternalMemoryWin32 ? 1 : 0);
			if (!m_ExternalMemorySupported)
			{
				// 明确记录、不静默：后续走"不导出句柄"的 CPU 读回；共享句柄判据会以
				// E_SPIKE_SHARED_HANDLE 判红（证据里保留这条读数）。
				CA_LOG_ERR("[渲染原型] Vulkan 设备不支持 VK_KHR_external_memory_win32：共享句柄不可用，"
				           "本批退化为不导出的 CPU 读回（不静默）");
			}

			std::vector<vk::QueueFamilyProperties> families = m_PhysicalDevice.getQueueFamilyProperties();
			bool foundQueue = false;
			for (uint32_t index = 0; index < static_cast<uint32_t>(families.size()); ++index)
			{
				if ((families[index].queueFlags & vk::QueueFlagBits::eGraphics) != vk::QueueFlags{})
				{
					m_QueueFamily = index;
					foundQueue = true;
					break;
				}
			}
			if (!foundQueue)
			{
				return Fail("Vulkan 物理设备没有图形队列族");
			}

			float priority = 1.0f;
			vk::DeviceQueueCreateInfo queueInfo{};
			queueInfo.queueFamilyIndex = m_QueueFamily;
			queueInfo.queueCount = 1;
			queueInfo.pQueuePriorities = &priority;
			vk::DeviceCreateInfo deviceInfo{};
			deviceInfo.queueCreateInfoCount = 1;
			deviceInfo.pQueueCreateInfos = &queueInfo;
			deviceInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
			deviceInfo.ppEnabledExtensionNames = enabledExtensions.empty() ? nullptr : enabledExtensions.data();
			// 不启用任何设备特性（只要图形 + 外部内存扩展）；VkPhysicalDeviceFeatures 留空。
			m_Device = m_PhysicalDevice.createDevice(deviceInfo);
			m_Queue = m_Device.getQueue(m_QueueFamily, 0);

			vk::CommandPoolCreateInfo poolInfo{};
			poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
			poolInfo.queueFamilyIndex = m_QueueFamily;
			m_CommandPool = m_Device.createCommandPool(poolInfo);
			vk::CommandBufferAllocateInfo allocateInfo{};
			allocateInfo.commandPool = m_CommandPool;
			allocateInfo.level = vk::CommandBufferLevel::ePrimary;
			allocateInfo.commandBufferCount = 1;
			m_CommandBuffer = m_Device.allocateCommandBuffers(allocateInfo).front();
			vk::FenceCreateInfo fenceInfo{};
			m_Fence = m_Device.createFence(fenceInfo);
			return true;
		}
		catch (vk::SystemError const& error)
		{
			return Fail(std::string("创建设备/命令设施失败：") + error.what());
		}
	}

	uint32_t VulkanWebRenderSpikeTexture::FindMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags required) const
	{
		vk::PhysicalDeviceMemoryProperties properties = m_PhysicalDevice.getMemoryProperties();
		for (uint32_t index = 0; index < properties.memoryTypeCount; ++index)
		{
			const bool allowed = (typeBits & (1u << index)) != 0u;
			const bool matches = (properties.memoryTypes[index].propertyFlags & required) == required;
			if (allowed && matches)
			{
				return index;
			}
		}
		return UINT32_MAX;
	}

	bool VulkanWebRenderSpikeTexture::CreateImage()
	{
		try
		{
			vk::ExternalMemoryImageCreateInfo externalInfo{};
			externalInfo.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;

			vk::ImageCreateInfo imageInfo{};
			imageInfo.imageType = vk::ImageType::e2D;
			imageInfo.format = vk::Format::eR8G8B8A8Unorm;
			imageInfo.extent = vk::Extent3D(m_Width, m_Height, 1);
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.samples = vk::SampleCountFlagBits::e1;
			imageInfo.tiling = vk::ImageTiling::eOptimal;
			imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
			imageInfo.sharingMode = vk::SharingMode::eExclusive;
			imageInfo.initialLayout = vk::ImageLayout::eUndefined;
			if (m_ExternalMemorySupported)
			{
				imageInfo.pNext = &externalInfo;
			}
			m_Image = m_Device.createImage(imageInfo);

			vk::MemoryRequirements requirements = m_Device.getImageMemoryRequirements(m_Image);
			const uint32_t memoryType = FindMemoryType(requirements.memoryTypeBits,
			                                           vk::MemoryPropertyFlagBits::eDeviceLocal);
			if (memoryType == UINT32_MAX)
			{
				return Fail("找不到满足图像要求的 DEVICE_LOCAL 内存类型");
			}
			vk::MemoryAllocateInfo allocateInfo{};
			allocateInfo.allocationSize = requirements.size;
			allocateInfo.memoryTypeIndex = memoryType;
			vk::MemoryDedicatedAllocateInfo dedicated{};
			dedicated.image = m_Image;
			vk::ExportMemoryAllocateInfo exportInfo{};
			exportInfo.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
			if (m_ExternalMemorySupported)
			{
				dedicated.pNext = &exportInfo;
				allocateInfo.pNext = &dedicated;
			}
			m_Memory = m_Device.allocateMemory(allocateInfo);
			m_MemorySize = requirements.size;
			m_MemoryTypeIndex = memoryType;
			m_Device.bindImageMemory(m_Image, m_Memory, 0);

			if (m_ExternalMemorySupported)
			{
				auto getWin32Handle = reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(
					m_Device.getProcAddr("vkGetMemoryWin32HandleKHR"));
				if (getWin32Handle == nullptr)
				{
					return Fail("取不到 vkGetMemoryWin32HandleKHR（扩展未启用）");
				}
				VkMemoryGetWin32HandleInfoKHR handleInfo{};
				handleInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
				handleInfo.memory = static_cast<VkDeviceMemory>(m_Memory);
				handleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
				const VkResult result = getWin32Handle(static_cast<VkDevice>(m_Device), &handleInfo, &m_SharedHandle);
				if (result != VK_SUCCESS || m_SharedHandle == nullptr)
				{
					m_SharedHandle = nullptr;
					return Fail(std::string("vkGetMemoryWin32HandleKHR 失败：") + ResultName(result));
				}
			}

			vk::ImageViewCreateInfo viewInfo{};
			viewInfo.image = m_Image;
			viewInfo.viewType = vk::ImageViewType::e2D;
			viewInfo.format = vk::Format::eR8G8B8A8Unorm;
			viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;
			m_View = m_Device.createImageView(viewInfo);
			return true;
		}
		catch (vk::SystemError const& error)
		{
			return Fail(std::string("创建共享图像失败：") + error.what());
		}
	}

	bool VulkanWebRenderSpikeTexture::CreatePipeline()
	{
		try
		{
			vk::ShaderModuleCreateInfo vertexInfo{};
			vertexInfo.codeSize = sizeof(kSpikeVertexSpirv);
			vertexInfo.pCode = kSpikeVertexSpirv;
			m_VertexShader = m_Device.createShaderModule(vertexInfo);
			vk::ShaderModuleCreateInfo fragmentInfo{};
			fragmentInfo.codeSize = sizeof(kSpikeFragmentSpirv);
			fragmentInfo.pCode = kSpikeFragmentSpirv;
			m_FragmentShader = m_Device.createShaderModule(fragmentInfo);

			// 渲染通道：一张 R8G8B8A8_UNORM 颜色附件；初始 UNDEFINED（整幅由清屏+绘制覆盖），
			// 最终 GENERAL（外部内存共享图像的通用布局，第二消费者也用 GENERAL 读）。
			vk::AttachmentDescription attachment{};
			attachment.format = vk::Format::eR8G8B8A8Unorm;
			attachment.samples = vk::SampleCountFlagBits::e1;
			attachment.loadOp = vk::AttachmentLoadOp::eClear;
			attachment.storeOp = vk::AttachmentStoreOp::eStore;
			attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			attachment.initialLayout = vk::ImageLayout::eUndefined;
			attachment.finalLayout = vk::ImageLayout::eGeneral;
			vk::AttachmentReference colorReference{};
			colorReference.attachment = 0;
			colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;
			vk::SubpassDescription subpass{};
			subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorReference;
			vk::SubpassDependency dependency{};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			dependency.srcAccessMask = vk::AccessFlags{};
			dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
			vk::RenderPassCreateInfo renderPassInfo{};
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.pAttachments = &attachment;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 1;
			renderPassInfo.pDependencies = &dependency;
			m_RenderPass = m_Device.createRenderPass(renderPassInfo);

			vk::FramebufferCreateInfo framebufferInfo{};
			framebufferInfo.renderPass = m_RenderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = &m_View;
			framebufferInfo.width = m_Width;
			framebufferInfo.height = m_Height;
			framebufferInfo.layers = 1;
			m_Framebuffer = m_Device.createFramebuffer(framebufferInfo);

			vk::PushConstantRange pushRange{};
			pushRange.stageFlags = vk::ShaderStageFlagBits::eAllGraphics;
			pushRange.offset = 0;
			pushRange.size = kPushConstantBytes;
			vk::PipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges = &pushRange;
			m_PipelineLayout = m_Device.createPipelineLayout(layoutInfo);

			vk::PipelineShaderStageCreateInfo stages[2] = {};
			stages[0].stage = vk::ShaderStageFlagBits::eVertex;
			stages[0].module = m_VertexShader;
			stages[0].pName = "main";
			stages[1].stage = vk::ShaderStageFlagBits::eFragment;
			stages[1].module = m_FragmentShader;
			stages[1].pName = "main";

			vk::PipelineVertexInputStateCreateInfo vertexInput{};
			vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
			vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(m_Width), static_cast<float>(m_Height), 0.0f, 1.0f);
			vk::Rect2D scissor({ 0, 0 }, { m_Width, m_Height });
			vk::PipelineViewportStateCreateInfo viewportState{};
			viewportState.viewportCount = 1;
			viewportState.pViewports = &viewport;
			viewportState.scissorCount = 1;
			viewportState.pScissors = &scissor;
			vk::PipelineRasterizationStateCreateInfo rasterization{};
			rasterization.polygonMode = vk::PolygonMode::eFill;
			rasterization.cullMode = vk::CullModeFlagBits::eNone;
			rasterization.frontFace = vk::FrontFace::eCounterClockwise;
			rasterization.lineWidth = 1.0f;
			vk::PipelineMultisampleStateCreateInfo multisample{};
			multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;
			vk::PipelineColorBlendAttachmentState blendAttachment{};
			blendAttachment.blendEnable = VK_FALSE;
			blendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
				vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
			vk::PipelineColorBlendStateCreateInfo colorBlend{};
			colorBlend.attachmentCount = 1;
			colorBlend.pAttachments = &blendAttachment;

			vk::GraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = stages;
			pipelineInfo.pVertexInputState = &vertexInput;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterization;
			pipelineInfo.pMultisampleState = &multisample;
			pipelineInfo.pColorBlendState = &colorBlend;
			pipelineInfo.layout = m_PipelineLayout;
			pipelineInfo.renderPass = m_RenderPass;
			pipelineInfo.subpass = 0;
			m_Pipeline = m_Device.createGraphicsPipeline(nullptr, pipelineInfo).value;
			return true;
		}
		catch (vk::SystemError const& error)
		{
			return Fail(std::string("创建渲染通道/管线失败：") + error.what());
		}
	}

	bool VulkanWebRenderSpikeTexture::SubmitAndWait(vk::CommandBuffer commandBuffer)
	{
		try
		{
			commandBuffer.end();
			vk::SubmitInfo submitInfo{};
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;
			m_Queue.submit(submitInfo, m_Fence);
			(void)m_Device.waitForFences(m_Fence, VK_TRUE, UINT64_MAX);
			(void)m_Device.resetFences(m_Fence);
			return true;
		}
		catch (vk::SystemError const& error)
		{
			return Fail(std::string("提交/等待 GPU 失败：") + error.what());
		}
	}

	bool VulkanWebRenderSpikeTexture::RenderFrame(uint32_t frameIndex)
	{
		m_LastError.clear();
		if (!m_Device || !m_Pipeline)
		{
			return Fail("共享图像尚未创建（RenderFrame 之前必须先 Create）");
		}
		try
		{
			(void)m_Device.resetCommandPool(m_CommandPool);
			vk::CommandBufferBeginInfo beginInfo{};
			beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
			m_CommandBuffer.begin(beginInfo);

			// 清屏 = 背景绿 (0,255,0,255)（R8G8B8A8_UNORM，1.0 → 255）。
			vk::ClearColorValue clearColor{};
			clearColor.float32[0] = 0.0f;
			clearColor.float32[1] = 1.0f;
			clearColor.float32[2] = 0.0f;
			clearColor.float32[3] = 1.0f;
			vk::ClearValue clearValue(clearColor);
			vk::RenderPassBeginInfo passInfo{};
			passInfo.renderPass = m_RenderPass;
			passInfo.framebuffer = m_Framebuffer;
			passInfo.renderArea = vk::Rect2D({ 0, 0 }, { m_Width, m_Height });
			passInfo.clearValueCount = 1;
			passInfo.pClearValues = &clearValue;
			m_CommandBuffer.beginRenderPass(passInfo, vk::SubpassContents::eInline);
			m_CommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_Pipeline);

			auto drawCorners = [this](float tlX, float tlY, float trX, float trY, float brX, float brY,
			                          float blX, float blY, float r, float g, float b, float a) {
				float constants[16] = {
					tlX, tlY, trX, trY,
					brX, brY, blX, blY,
					r, g, b, a,
					static_cast<float>(m_Width), static_cast<float>(m_Height), 0.0f, 0.0f,
				};
				m_CommandBuffer.pushConstants(m_PipelineLayout, vk::ShaderStageFlagBits::eAllGraphics, 0,
				                              kPushConstantBytes, constants);
				m_CommandBuffer.draw(6, 1, 0, 0);
			};
			auto drawRect = [&drawCorners](float x0, float y0, float x1, float y1, float r, float g, float b,
			                               float a) {
				drawCorners(x0, y0, x1, y0, x1, y1, x0, y1, r, g, b, a);
			};

			// ② 中心红三角 (255,0,0,255)：顶点 (128,64)，底边 y=192、x 从 64 到 192（与 D3D12 同一版式）。
			drawCorners(128.0f, 64.0f, 128.0f, 64.0f, 192.0f, 192.0f, 64.0f, 192.0f, 1.0f, 0.0f, 0.0f, 1.0f);
			// ③ 四角标记：8×8 红方块（内缩 4 像素）。
			drawRect(4.0f, 4.0f, 12.0f, 12.0f, 1.0f, 0.0f, 0.0f, 1.0f);
			drawRect(244.0f, 4.0f, 252.0f, 12.0f, 1.0f, 0.0f, 0.0f, 1.0f);
			drawRect(4.0f, 244.0f, 12.0f, 252.0f, 1.0f, 0.0f, 0.0f, 1.0f);
			drawRect(244.0f, 244.0f, 252.0f, 252.0f, 1.0f, 0.0f, 0.0f, 1.0f);
			// ④ 帧序号：顶边 16 位二进制，每比特 8×8（白=1、黑=0），从 x=32 起排。
			for (uint32_t bit = 0; bit < 16; ++bit)
			{
				const bool on = ((frameIndex >> (15u - bit)) & 1u) != 0u;
				const float x0 = kFrameBitOriginX + static_cast<float>(bit) * kFrameBitSize;
				const float y0 = kFrameBitOriginY;
				const float value = on ? 1.0f : 0.0f;
				drawRect(x0, y0, x0 + kFrameBitSize, y0 + kFrameBitSize, value, value, value, 1.0f);
			}
			m_CommandBuffer.endRenderPass();

			// 渲染通道 finalLayout = GENERAL；这里补一条"颜色附件写 → 拷贝读"的屏障，
			// 之后 CopyImageToBuffer 直接按 GENERAL 读。
			vk::ImageMemoryBarrier barrier{};
			barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
			barrier.oldLayout = vk::ImageLayout::eGeneral;
			barrier.newLayout = vk::ImageLayout::eGeneral;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = m_Image;
			barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			m_CommandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
			                               vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier);
			const bool submitted = SubmitAndWait(m_CommandBuffer);
			if (submitted)
			{
				m_HasRendered = true;
			}
			return submitted;
		}
		catch (vk::SystemError const& error)
		{
			return Fail(std::string("渲染第 ") + std::to_string(frameIndex) + " 帧失败：" + error.what());
		}
	}

	bool VulkanWebRenderSpikeTexture::CopyImageToHost(vk::Image image, bool importedImage, uint8_t* dst,
	                                                  uint64_t capacity, char const* what)
	{
		if (dst == nullptr)
		{
			return Fail(std::string(what) + "：目标缓冲区为空");
		}
		const uint64_t required = static_cast<uint64_t>(m_Width) * static_cast<uint64_t>(m_Height) * 4ull;
		if (capacity < required)
		{
			return Fail(std::string(what) + "：目标缓冲区太小（" + std::to_string(capacity) + " < " +
			            std::to_string(required) + " 字节）");
		}
		try
		{
			vk::BufferCreateInfo bufferInfo{};
			bufferInfo.size = required;
			bufferInfo.usage = vk::BufferUsageFlagBits::eTransferDst;
			bufferInfo.sharingMode = vk::SharingMode::eExclusive;
			vk::Buffer buffer = m_Device.createBuffer(bufferInfo);
			vk::MemoryRequirements requirements = m_Device.getBufferMemoryRequirements(buffer);
			const uint32_t memoryType = FindMemoryType(requirements.memoryTypeBits,
			                                           vk::MemoryPropertyFlagBits::eHostVisible |
			                                           vk::MemoryPropertyFlagBits::eHostCoherent);
			if (memoryType == UINT32_MAX)
			{
				m_Device.destroyBuffer(buffer);
				return Fail(std::string(what) + "：找不到 HOST_VISIBLE|HOST_COHERENT 内存类型");
			}
			vk::MemoryAllocateInfo allocateInfo{};
			allocateInfo.allocationSize = requirements.size;
			allocateInfo.memoryTypeIndex = memoryType;
			vk::DeviceMemory memory = m_Device.allocateMemory(allocateInfo);
			m_Device.bindBufferMemory(buffer, memory, 0);

			(void)m_Device.resetCommandPool(m_CommandPool);
			vk::CommandBufferBeginInfo beginInfo{};
			beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
			m_CommandBuffer.begin(beginInfo);
			if (importedImage || !m_HasRendered)
			{
				// 导入得到的图像对象对本设备是新的；没渲染过的生产者也还是 UNDEFINED 布局 ——
				// 两种都按 UNDEFINED → GENERAL 过渡（渲染过的生产者已由 RenderFrame 里的屏障确保 GENERAL）。
				vk::ImageMemoryBarrier barrier{};
				barrier.srcAccessMask = vk::AccessFlags{};
				barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
				barrier.oldLayout = vk::ImageLayout::eUndefined;
				barrier.newLayout = vk::ImageLayout::eGeneral;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = image;
				barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
				barrier.subresourceRange.baseMipLevel = 0;
				barrier.subresourceRange.levelCount = 1;
				barrier.subresourceRange.baseArrayLayer = 0;
				barrier.subresourceRange.layerCount = 1;
				m_CommandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
				                                vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier);
			}
			vk::BufferImageCopy region{};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
			region.imageSubresource.mipLevel = 0;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = 1;
			region.imageOffset = vk::Offset3D(0, 0, 0);
			region.imageExtent = vk::Extent3D(m_Width, m_Height, 1);
			m_CommandBuffer.copyImageToBuffer(image, vk::ImageLayout::eGeneral, buffer, 1, &region);
			if (!SubmitAndWait(m_CommandBuffer))
			{
				m_Device.destroyBuffer(buffer);
				m_Device.freeMemory(memory);
				return false;
			}

			void* mapped = m_Device.mapMemory(memory, 0, VK_WHOLE_SIZE);
			std::memcpy(dst, mapped, static_cast<size_t>(required));
			m_Device.unmapMemory(memory);
			m_Device.destroyBuffer(buffer);
			m_Device.freeMemory(memory);
			return true;
		}
		catch (vk::SystemError const& error)
		{
			return Fail(std::string(what) + "：读回失败：" + error.what());
		}
	}

	bool VulkanWebRenderSpikeTexture::Readback(uint8_t* dst, uint64_t capacity)
	{
		m_LastError.clear();
		if (!m_Image)
		{
			return Fail("共享图像尚未创建（Readback 之前必须先 Create）");
		}
		return CopyImageToHost(m_Image, false, dst, capacity, "本设备读回");
	}

	bool VulkanWebRenderSpikeTexture::EnsureImportedImage()
	{
		if (m_ImportedImage)
		{
			return true;
		}
		if (m_SharedHandle == nullptr)
		{
			return Fail("共享句柄为空（第二消费者无从导入）");
		}
		try
		{
			vk::ExternalMemoryImageCreateInfo externalInfo{};
			externalInfo.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
			vk::ImageCreateInfo imageInfo{};
			imageInfo.imageType = vk::ImageType::e2D;
			imageInfo.format = vk::Format::eR8G8B8A8Unorm;
			imageInfo.extent = vk::Extent3D(m_Width, m_Height, 1);
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.samples = vk::SampleCountFlagBits::e1;
			imageInfo.tiling = vk::ImageTiling::eOptimal;
			imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
			imageInfo.sharingMode = vk::SharingMode::eExclusive;
			imageInfo.initialLayout = vk::ImageLayout::eUndefined;
			imageInfo.pNext = &externalInfo;
			m_ImportedImage = m_Device.createImage(imageInfo);

			vk::MemoryRequirements requirements = m_Device.getImageMemoryRequirements(m_ImportedImage);
			if (m_MemoryTypeIndex == UINT32_MAX || (requirements.memoryTypeBits & (1u << m_MemoryTypeIndex)) == 0u)
			{
				return Fail("第二消费者：导入图像不接受生产者的内存类型（句柄不兼容）");
			}
			// 规范允许把同一 payload 导入导出它的同一个实例；每次导入必须创建独立的 VkDeviceMemory。
			VkMemoryDedicatedAllocateInfo dedicated{};
			dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
			dedicated.image = static_cast<VkImage>(m_ImportedImage);
			VkImportMemoryWin32HandleInfoKHR importInfo{};
			importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
			importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
			importInfo.handle = m_SharedHandle;
			importInfo.pNext = &dedicated;
			vk::MemoryAllocateInfo allocateInfo{};
			allocateInfo.allocationSize = m_MemorySize;
			allocateInfo.memoryTypeIndex = m_MemoryTypeIndex;
			allocateInfo.pNext = &importInfo;
			m_ImportedMemory = m_Device.allocateMemory(allocateInfo);
			m_Device.bindImageMemory(m_ImportedImage, m_ImportedMemory, 0);
			return true;
		}
		catch (vk::SystemError const& error)
		{
			return Fail(std::string("第二消费者导入共享句柄失败：") + error.what());
		}
	}

	bool VulkanWebRenderSpikeTexture::ReadbackViaOpenedHandle(uint8_t* dst, uint64_t capacity)
	{
		m_LastError.clear();
		if (!m_ExternalMemorySupported)
		{
			return Fail("Vulkan 设备不支持 VK_KHR_external_memory_win32（已记录；本批退 CPU 读回，不导出句柄）");
		}
		if (!EnsureImportedImage())
		{
			return false;
		}
		return CopyImageToHost(m_ImportedImage, true, dst, capacity, "第二消费者读回");
	}

	void VulkanWebRenderSpikeTexture::ReleaseResources()
	{
		// 注意：hpp 的 `handle = nullptr` 只是把句柄置空、**不销毁**对象，所以这里逐个显式销毁，
		// 顺序按"依赖者先走"：导入图像/内存 → 管线 → 帧缓冲 → 渲染通道 → 视图/图像/内存 →
		// 着色器模块 → fence/命令池 → 设备。
		if (m_Device)
		{
			(void)m_Device.waitIdle();
			if (m_ImportedImage)
			{
				m_Device.destroyImage(m_ImportedImage);
				m_ImportedImage = nullptr;
			}
			if (m_ImportedMemory)
			{
				m_Device.freeMemory(m_ImportedMemory);
				m_ImportedMemory = nullptr;
			}
			if (m_Pipeline)
			{
				m_Device.destroyPipeline(m_Pipeline);
				m_Pipeline = nullptr;
			}
			if (m_PipelineLayout)
			{
				m_Device.destroyPipelineLayout(m_PipelineLayout);
				m_PipelineLayout = nullptr;
			}
			if (m_Framebuffer)
			{
				m_Device.destroyFramebuffer(m_Framebuffer);
				m_Framebuffer = nullptr;
			}
			if (m_RenderPass)
			{
				m_Device.destroyRenderPass(m_RenderPass);
				m_RenderPass = nullptr;
			}
			if (m_View)
			{
				m_Device.destroyImageView(m_View);
				m_View = nullptr;
			}
			if (m_Image)
			{
				m_Device.destroyImage(m_Image);
				m_Image = nullptr;
			}
			if (m_Memory)
			{
				m_Device.freeMemory(m_Memory);
				m_Memory = nullptr;
			}
			if (m_VertexShader)
			{
				m_Device.destroyShaderModule(m_VertexShader);
				m_VertexShader = nullptr;
			}
			if (m_FragmentShader)
			{
				m_Device.destroyShaderModule(m_FragmentShader);
				m_FragmentShader = nullptr;
			}
			if (m_Fence)
			{
				m_Device.destroyFence(m_Fence);
				m_Fence = nullptr;
			}
			if (m_CommandPool)
			{
				m_Device.destroyCommandPool(m_CommandPool);
				m_CommandPool = nullptr;
			}
			m_CommandBuffer = nullptr;
			m_Queue = nullptr;
			m_Device.destroy();
			m_Device = nullptr;
		}
		m_ImportedImage = nullptr;
		m_ImportedMemory = nullptr;
		m_Pipeline = nullptr;
		m_PipelineLayout = nullptr;
		m_Framebuffer = nullptr;
		m_RenderPass = nullptr;
		m_View = nullptr;
		m_Image = nullptr;
		m_Memory = nullptr;
		m_VertexShader = nullptr;
		m_FragmentShader = nullptr;
		m_Fence = nullptr;
		m_CommandPool = nullptr;
		m_CommandBuffer = nullptr;
		m_Queue = nullptr;
		if (m_SharedHandle != nullptr)
		{
			// vkGetMemoryWin32HandleKHR 返回的是 NT 句柄：应用持有引用，必须 CloseHandle（规范原文，见 5.1）。
			CloseHandle(m_SharedHandle);
			m_SharedHandle = nullptr;
		}
		m_ExternalMemorySupported = false;
		m_HasRendered = false;
		m_MemorySize = 0;
		m_MemoryTypeIndex = UINT32_MAX;
		m_Width = 0;
		m_Height = 0;
	}

	void VulkanWebRenderSpikeTexture::Release()
	{
		ReleaseResources();
	}

	VulkanWebRenderSpikeProvider::VulkanWebRenderSpikeProvider(RenderBackend_Vulkan* backend)
		: m_Backend(backend)
	{
	}

	VulkanWebRenderSpikeProvider::~VulkanWebRenderSpikeProvider()
	{
		m_Texture.reset();
		m_Backend = nullptr;
	}

	IWebRenderSpikeSharedTexture* VulkanWebRenderSpikeProvider::GetWebRenderSpikeTexture()
	{
		if (m_Backend == nullptr)
		{
			return nullptr;
		}
		if (m_Texture == nullptr)
		{
			m_Texture = std::make_unique<VulkanWebRenderSpikeTexture>(m_Backend);
		}
		return m_Texture.get();
	}
}
