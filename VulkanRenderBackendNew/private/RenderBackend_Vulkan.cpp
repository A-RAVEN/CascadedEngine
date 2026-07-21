#include <RenderBackend_Vulkan.h>
#include <Utils/VulkanDebug.h>
#include <VulkanObjects/VulkanBuffer.h>
#include <VulkanObjects/VulkanTexture.h>
#include <VulkanObjects/VulkanWindowHandle.h>
#include <VulkanObjects/VulkanShaderStruct.h>

#define CA_IMPLEMENT_MODULE 1
#include <CACore/CAModuleImplementation.h>

// SDK 1.4+: defaultDispatchLoaderDynamic moved to vk::detail:: namespace
namespace vk::detail {
	DispatchLoaderDynamic defaultDispatchLoaderDynamic;
}

// Global validation log file + setter (exported for GetProcAddress in Main.cpp)
static FILE* g_ValidationLogFile = nullptr;

extern "C" __declspec(dllexport) void SetValidationLogFile(FILE* file)
{
	g_ValidationLogFile = file;
}

namespace graphics_backend
{

	static castl::vector<const char*> GetInstanceExtensionNames()
	{
		return castl::vector<const char*>{
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
				VK_KHR_SURFACE_EXTENSION_NAME,
#if defined( VK_USE_PLATFORM_ANDROID_KHR )
				VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_IOS_MVK )
				VK_MVK_IOS_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_MACOS_MVK )
				VK_MVK_MACOS_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_MIR_KHR )
				VK_KHR_MIR_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_VI_NN )
				VK_NN_VI_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_WAYLAND_KHR )
				VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_WIN32_KHR )
				VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_XCB_KHR )
				VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_XLIB_KHR )
				VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#elif defined( VK_USE_PLATFORM_XLIB_XRANDR_EXT )
				VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME,
#endif
		};
	}

	static castl::vector<const char*> GetDeviceExtensionNames()
	{
		return castl::vector<const char*>{
			VK_KHR_MAINTENANCE_4_EXTENSION_NAME,
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
			VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME,
		};
	}

	VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT       messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT              messageTypes,
		VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData,
		void* /*pUserData*/)
	{
#if !defined( NDEBUG )
		if (pCallbackData->messageIdNumber == 648835635)
		{
			// UNASSIGNED-khronos-Validation-debug-build-warning-message
			return VK_FALSE;
		}
		if (pCallbackData->messageIdNumber == 767975156)
		{
			// UNASSIGNED-BestPractices-vkCreateInstance-specialuse-extension
			return VK_FALSE;
		}
#endif

		if (pCallbackData->pMessageIdName != NULL)
		{
			castl::stringstream messageStream;
			messageStream << vk::to_string(static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(messageSeverity)) << ": "
				<< vk::to_string(static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(messageTypes)) << ":\n";
			messageStream << std::string("\t") << "messageIDName   = <" << pCallbackData->pMessageIdName << ">\n";
			messageStream << std::string("\t") << "messageIdNumber = " << pCallbackData->messageIdNumber << "\n";
			messageStream << std::string("\t") << "message         = <" << pCallbackData->pMessage << ">\n";
			if (0 < pCallbackData->queueLabelCount)
			{
				messageStream << std::string("\t") << "Queue Labels:\n";
				for (uint32_t i = 0; i < pCallbackData->queueLabelCount; i++)
				{
					messageStream << std::string("\t\t") << "labelName = <" << pCallbackData->pQueueLabels[i].pLabelName << ">\n";
				}
			}
			if (0 < pCallbackData->cmdBufLabelCount)
			{
				messageStream << std::string("\t") << "CommandBuffer Labels:\n";
				for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; i++)
				{
					messageStream << std::string("\t\t") << "labelName = <" << pCallbackData->pCmdBufLabels[i].pLabelName << ">\n";
				}
			}
			if (0 < pCallbackData->objectCount)
			{
				messageStream << std::string("\t") << "Objects:\n";
				for (uint32_t i = 0; i < pCallbackData->objectCount; i++)
				{
					messageStream << std::string("\t\t") << "Object " << i << "\n";
					messageStream << std::string("\t\t\t") << "objectType   = " << vk::to_string(static_cast<vk::ObjectType>(pCallbackData->pObjects[i].objectType))
						<< "\n";
					messageStream << std::string("\t\t\t") << "objectHandle = " << pCallbackData->pObjects[i].objectHandle << "\n";
					if (pCallbackData->pObjects[i].pObjectName)
					{
						messageStream << std::string("\t\t\t") << "objectName   = <" << pCallbackData->pObjects[i].pObjectName << ">\n";
					}
				}
			}
				// Task 4.3: Write to validation log file if set
				if (g_ValidationLogFile)
				{
					fprintf(g_ValidationLogFile, "%s\n", messageStream.str().c_str());
					fflush(g_ValidationLogFile);
				}
				CA_LOG_ERR("///////////////////\n{}\n///////////////////", messageStream.str());
			if (messageSeverity == VkDebugUtilsMessageSeverityFlagBitsEXT::VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			{
#if BREAK_ON_VULKAN_ERROR
				__debugbreak();
#endif
			}
		}

		return VK_FALSE;
	}


	void RenderBackend_Vulkan::Init(cacore::IModuleManager* pModuleManager)
	{
		p_ResourceManager = pModuleManager->GetInstance<resource_management::ResourceManagingSystem>();

		// Register shader importer (compiles .slang → SPIR-V into VulkanShaderLibrary.shLib)
		{
			auto shaderCompiler = pModuleManager->GetInstance<ShaderCompilerSlang::IShaderCompilerManager>();
			auto p_ResourceImporter = pModuleManager->GetInstance<resource_management::ResourceImportingSystem>();
			if (shaderCompiler && p_ResourceImporter)
			{
				m_ShaderImporter.SetCompiler(shaderCompiler);
				p_ResourceImporter->AddImporter(&m_ShaderImporter);
			}
		}

		VULKAN_HPP_DEFAULT_DISPATCHER.init();

		vk::ApplicationInfo application_info(
			"Test Backend"
			, 1
			, "Test Engine"
			, 0
			, VULKAN_API_VERSION_IN_USE);

		const castl::vector<const char*> g_validationLayers{
			 
		};

		auto extensions = GetInstanceExtensionNames();
		vk::InstanceCreateInfo instance_info({}, &application_info, g_validationLayers, extensions);

		vk::DebugUtilsMessengerCreateInfoEXT debugUtilsExt{ {},
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
			| vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
			vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral
			| vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance
			| vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
			&debugUtilsMessengerCallback };
		instance_info.setPNext(&debugUtilsExt);
		m_VulkanInstance = vk::createInstance(instance_info);
		VULKAN_HPP_DEFAULT_DISPATCHER.init(m_VulkanInstance);
		m_DebugMessenger = m_VulkanInstance.createDebugUtilsMessengerEXT(debugUtilsExt);
		//Init Device
		m_PhysicalDevice = m_VulkanInstance.enumeratePhysicalDevices().front();
		InitSubObj(&m_QueueContext);
		auto deviceExts = GetDeviceExtensionNames();
		QueueContext::QueueCreationInfo queueCreationInfo{};
		m_QueueContext.InitQueueCreationInfo(m_PhysicalDevice, queueCreationInfo);
		vk::DeviceCreateInfo deviceCreateInfo({}, queueCreationInfo.queueCreateInfoList, {}, deviceExts);
		m_Device = m_PhysicalDevice.createDevice(deviceCreateInfo);
		VULKAN_HPP_DEFAULT_DISPATCHER.init(m_Device);

		//Init Object Containers
		InitSubObj(&m_DescriptorSetLayoutContainer);

		// Init Sampler Manager
		InitSubObj(&m_SamplerManager);

		// Init Memory Manager
		InitSubObj(&m_MemoryManager);
		m_MemoryManager.Init();

		// Init Command List Manager
		InitSubObj(&m_CommandListManager);
		m_CommandListManager.Init();

		// Check for pipeline library support
		auto deviceExtensions = m_PhysicalDevice.enumerateDeviceExtensionProperties();
		for (auto const& ext : deviceExtensions)
		{
			if (strcmp(ext.extensionName, VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME) == 0)
			{
				m_PipelineLibrarySupported = true;
				CA_LOG_INFO("VK_EXT_graphics_pipeline_library supported");
				break;
			}
		}

		// Init Pipeline Library
		InitSubObj(&m_PipelineLibrary);
		m_PipelineLibrary.Init();

		// Init Pipeline Library Cache
		InitSubObj(&m_PipelineLibraryCache);
		m_PipelineLibraryCache.Init();

		// Init Pipeline Cache (try to load from disk)
		{
			vk::PipelineCacheCreateInfo cacheInfo{};
			castl::vector<uint8_t> cacheData;
			FILE* cacheFile = fopen("pipeline_cache.bin", "rb");
			if (cacheFile)
			{
				fseek(cacheFile, 0, SEEK_END);
				long fileSize = ftell(cacheFile);
				fseek(cacheFile, 0, SEEK_SET);
				if (fileSize > 0)
				{
					cacheData.resize(fileSize);
					fread(cacheData.data(), 1, fileSize, cacheFile);
					cacheInfo.initialDataSize = cacheData.size();
					cacheInfo.pInitialData = cacheData.data();
				}
				fclose(cacheFile);
			}
			try
			{
				m_PipelineCache = m_Device.createPipelineCache(cacheInfo);
			}
			catch (vk::SystemError const& e)
			{
				CA_LOG_WARN("RenderBackend_Vulkan: Failed to load pipeline cache from disk, creating empty: {}", e.what());
				cacheInfo.initialDataSize = 0;
				cacheInfo.pInitialData = nullptr;
				m_PipelineCache = m_Device.createPipelineCache(cacheInfo);
			}
			CA_LOG_INFO("RenderBackend_Vulkan: Pipeline cache created (loaded {} bytes from disk)", cacheData.size());
		}

		// Init GPU Frame Manager
		InitSubObj(&m_GPUFrameManager);
		m_GPUFrameManager.Init();

		CA_LOG_INFO("VulkanRenderBackend initialized successfully");
	}
	void RenderBackend_Vulkan::ExecuteGraph(TaskScheduler* scheduler, castl::shared_ptr<GPUGraph> const& graph)
	{
		CA_LOG_INFO("RenderBackend_Vulkan: ExecuteGraph entry");
		if (!graph)
		{
			CA_LOG_WARN("RenderBackend_Vulkan::ExecuteGraph - null graph");
			return;
		}

		// Aquire frame context from GPUFrameManager (round-robin, waits for previous GPU work)
		CA_LOG_INFO("RenderBackend_Vulkan: AquireFrameContext...");
		auto frameContext = m_GPUFrameManager.AquireFrameContext();
		CA_LOG_INFO("RenderBackend_Vulkan: AquireFrameContext done");

		// Create graph executor (per-frame, like D3D12)
		VulkanGraphExecutor executor;
		InitSubObj(&executor);

		// Execute graph with frame context
		CA_LOG_INFO("RenderBackend_Vulkan: calling CompileAndExecute");
		executor.CompileAndExecute(graph, std::move(frameContext));

		// Release executor (frame context auto-released by PFrameContext deleter)
		executor.Release();
	}
	void RenderBackend_Vulkan::Release()
	{
		// Release all window handles before destroying device/instance
		for (auto& [window, weakHandle] : m_WindowHandles)
		{
			if (auto handle = weakHandle.lock())
			{
				static_cast<VulkanWindowHandle*>(handle.get())->Release();
			}
		}
		m_WindowHandles.clear();

		// Release GPU Frame Manager
		m_GPUFrameManager.WaitIdle();
		m_GPUFrameManager.Release();

		// Serialize pipeline cache to disk before destroying
		if (m_PipelineCache)
		{
			try
			{
				auto cacheData = m_Device.getPipelineCacheData(m_PipelineCache);
				if (!cacheData.empty())
				{
					FILE* cacheFile = fopen("pipeline_cache.bin", "wb");
					if (cacheFile)
					{
						fwrite(cacheData.data(), 1, cacheData.size(), cacheFile);
						fclose(cacheFile);
						CA_LOG_INFO("RenderBackend_Vulkan: Pipeline cache serialized to disk ({} bytes)", cacheData.size());
					}
				}
				m_Device.destroyPipelineCache(m_PipelineCache);
				m_PipelineCache = nullptr;
			}
			catch (vk::SystemError const& e)
			{
				CA_LOG_WARN("RenderBackend_Vulkan: Failed to serialize pipeline cache: {}", e.what());
			}
		}

		// Release pipeline library cache
		m_PipelineLibraryCache.Release();

		// Release pipeline library
		m_PipelineLibrary.Release();

		// Cleanup cross-frame caches
		{
			auto device = m_Device;
			for (auto& [hash, framebuffer] : m_FramebufferCache)
			{
				if (framebuffer) device.destroyFramebuffer(framebuffer);
			}
			m_FramebufferCache.clear();
			for (auto& [hash, renderPass] : m_RenderPassCache)
			{
				if (renderPass) device.destroyRenderPass(renderPass);
			}
			m_RenderPassCache.clear();
			for (auto& [hash, shaderModule] : m_ShaderModuleCache)
			{
				if (shaderModule) device.destroyShaderModule(shaderModule);
			}
			m_ShaderModuleCache.clear();
			for (auto& [hash, pipelineLayout] : m_PipelineLayoutCache)
			{
				if (pipelineLayout) device.destroyPipelineLayout(pipelineLayout);
			}
			m_PipelineLayoutCache.clear();
			for (auto& [hash, setLayout] : m_DescriptorSetLayoutCache)
			{
				if (setLayout) device.destroyDescriptorSetLayout(setLayout);
			}
			m_DescriptorSetLayoutCache.clear();
		}

		// Release command list manager
		m_CommandListManager.Release();

		// Release sampler manager
		m_SamplerManager.Release();

		// Release memory manager
		m_MemoryManager.Release();

		// Release descriptor set layout container
		m_DescriptorSetLayoutContainer.Release();

		// Destroy device
		if (m_Device)
		{
			m_Device.waitIdle();
			m_Device.destroy();
			m_Device = nullptr;
		}

		// Destroy debug messenger
		if (m_DebugMessenger)
		{
			m_VulkanInstance.destroyDebugUtilsMessengerEXT(m_DebugMessenger);
			m_DebugMessenger = nullptr;
		}

		// Destroy instance
		if (m_VulkanInstance)
		{
			m_VulkanInstance.destroy();
			m_VulkanInstance = nullptr;
		}

		CA_LOG_INFO("VulkanRenderBackend released");
	}
	castl::shared_ptr<GPUBuffer> RenderBackend_Vulkan::CreateGPUBuffer(GPUBufferDescriptor const& descriptor, EBufferUsageFlags usageFlags)
	{
		auto buffer = castl::make_shared<VulkanBuffer>();
		InitSubObj(buffer.get());
		buffer->Init(descriptor, usageFlags);
		return buffer;
	}
	castl::shared_ptr<GPUTexture> RenderBackend_Vulkan::CreateGPUTexture(GPUTextureDescriptor const& inDescriptor, ETextureAccessTypeFlags accessType)
	{
		auto texture = castl::make_shared<VulkanTexture>();
		InitSubObj(texture.get());
		texture->Init(inDescriptor, accessType);
		return texture;
	}
	castl::shared_ptr<ShaderStruct> RenderBackend_Vulkan::CreateShaderStruct(cacore::NameHash const& structType)
	{
		auto shaderStruct = castl::make_shared<VulkanShaderStruct>();
		InitSubObj(shaderStruct.get());

		// Look up ShaderStructData from ShaderLibrary (references D3D12 pattern)
		ShaderCompilerSlang::ShaderStructData const* pData = nullptr;
		if (p_ResourceManager != nullptr)
		{
			auto shaderLibrary = p_ResourceManager->GetOrLoadResource<ShaderLibrary>("VulkanShaderLibrary.shLib");
			if (shaderLibrary != nullptr)
			{
				pData = shaderLibrary->GetShaderStruct(structType);
			}
		}

		shaderStruct->Init(pData);
		return shaderStruct;
	}

	VulkanShaderFileInfo const* RenderBackend_Vulkan::GetShaderFileInfo(ShaderInfo const& shaderInfo)
	{
		if (p_ResourceManager == nullptr)
			return nullptr;
		auto shaderLibrary = p_ResourceManager->GetOrLoadResource<ShaderLibrary>("VulkanShaderLibrary.shLib");
		if (shaderLibrary == nullptr)
			return nullptr;
		return shaderLibrary->GetShaderFileInfo(shaderInfo.path);
	}
	castl::shared_ptr<WindowHandle> RenderBackend_Vulkan::GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window)
	{
		if (!window)
			return nullptr;

		// Check for existing handle
		auto it = m_WindowHandles.find(window.get());
		if (it != m_WindowHandles.end())
		{
			auto existing = it->second.lock();
			if (existing)
				return existing;
		}

		// Create new handle
		auto windowHandle = castl::make_shared<VulkanWindowHandle>();
		InitSubObj(windowHandle.get());
		windowHandle->Init(window);

		m_WindowHandles[window.get()] = windowHandle;
		return windowHandle;
	}
	bool RenderBackend_Vulkan::AnyWindowRunning()
	{
		for (auto it = m_WindowHandles.begin(); it != m_WindowHandles.end(); )
		{
			auto handle = it->second.lock();
			if (!handle)
			{
				// Remove expired handles
				it = m_WindowHandles.erase(it);
			}
			else if (handle->IsValid())
			{
				return true;
			}
			else
			{
				++it;
			}
		}
		return false;
	}
}

// --- Cross-frame cache methods (moved from VulkanGraphExecutor) ---

namespace graphics_backend
{

vk::ShaderModule RenderBackend_Vulkan::GetOrCreateShaderModule(cahash::sha256_hash::result_type const& programHash)
{
	auto it = m_ShaderModuleCache.find(programHash);
	if (it != m_ShaderModuleCache.end())
		return it->second;

	if (p_ResourceManager == nullptr)
		return vk::ShaderModule(nullptr);

	auto shaderLibrary = p_ResourceManager->GetOrLoadResource<ShaderLibrary>("VulkanShaderLibrary.shLib");
	if (shaderLibrary == nullptr)
		return vk::ShaderModule(nullptr);

	VulkanShaderCode const* shaderCode = shaderLibrary->GetShaderCode(programHash);
	if (shaderCode == nullptr || shaderCode->spirvCode.empty())
		return vk::ShaderModule(nullptr);

	vk::ShaderModuleCreateInfo moduleInfo{};
	moduleInfo.codeSize = shaderCode->spirvCode.size() * sizeof(uint32_t);
	moduleInfo.pCode = shaderCode->spirvCode.data();

	try
	{
		auto shaderModule = m_Device.createShaderModule(moduleInfo);
		m_ShaderModuleCache[programHash] = shaderModule;
		return shaderModule;
	}
	catch (vk::SystemError const& e)
	{
		CA_LOG_ERR("RenderBackend_Vulkan: Failed to create shader module: {}", e.what());
		return vk::ShaderModule(nullptr);
	}
}

vk::DescriptorSetLayout RenderBackend_Vulkan::GetOrCreateDescriptorSetLayout(VulkanDescriptorSetLayoutInfo const& setLayoutInfo)
{
	size_t hash = setLayoutInfo.GetHash();
	auto it = m_DescriptorSetLayoutCache.find(hash);
	if (it != m_DescriptorSetLayoutCache.end())
		return it->second;

	castl::vector<vk::DescriptorSetLayoutBinding> vkBindings;
	auto createInfo = setLayoutInfo.GetCreateInfo(vkBindings);

	try
	{
		auto layout = m_Device.createDescriptorSetLayout(createInfo);
		m_DescriptorSetLayoutCache[hash] = layout;
		return layout;
	}
	catch (vk::SystemError const& e)
	{
		CA_LOG_ERR("RenderBackend_Vulkan: Failed to create descriptor set layout: {}", e.what());
		return vk::DescriptorSetLayout(nullptr);
	}
}

vk::PipelineLayout RenderBackend_Vulkan::GetOrCreatePipelineLayout(VulkanShaderResourceBindingInfo const& bindingInfo)
{
	size_t hash = 0;
	for (auto const& setLayoutInfo : bindingInfo.setLayoutInfos)
	{
		cacore::hash_combine(hash, setLayoutInfo.setIndex);
		for (auto const& binding : setLayoutInfo.bindings)
		{
			cacore::hash_combine(hash, binding.descriptorType);
			cacore::hash_combine(hash, binding.binding);
			cacore::hash_combine(hash, binding.descriptorCount);
			cacore::hash_combine(hash, binding.stageFlags);
		}
	}

	auto it = m_PipelineLayoutCache.find(hash);
	if (it != m_PipelineLayoutCache.end())
		return it->second;

	castl::vector<VulkanDescriptorSetLayoutInfo const*> sortedSetLayouts;
	sortedSetLayouts.reserve(bindingInfo.setLayoutInfos.size());
	for (auto const& setLayoutInfo : bindingInfo.setLayoutInfos)
		sortedSetLayouts.push_back(&setLayoutInfo);
	castl::sort(sortedSetLayouts.begin(), sortedSetLayouts.end(),
		[](VulkanDescriptorSetLayoutInfo const* a, VulkanDescriptorSetLayoutInfo const* b)
		{ return a->setIndex < b->setIndex; });

	castl::vector<vk::DescriptorSetLayout> setLayouts;
	for (auto const* pSetLayoutInfo : sortedSetLayouts)
	{
		auto layout = GetOrCreateDescriptorSetLayout(*pSetLayoutInfo);
		if (!layout) return vk::PipelineLayout(nullptr);
		setLayouts.push_back(layout);
	}

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
	pipelineLayoutInfo.pSetLayouts = setLayouts.data();

	try
	{
		auto pipelineLayout = m_Device.createPipelineLayout(pipelineLayoutInfo);
		m_PipelineLayoutCache[hash] = pipelineLayout;
		return pipelineLayout;
	}
	catch (vk::SystemError const& e)
	{
		CA_LOG_ERR("RenderBackend_Vulkan: Failed to create pipeline layout: {}", e.what());
		return vk::PipelineLayout(nullptr);
	}
}

vk::RenderPass RenderBackend_Vulkan::GetOrCreateRenderPass(RenderPassCacheKey const& key)
{
	size_t hash = 0;
	for (auto const& fmt : key.colorFormats)
		cacore::hash_combine(hash, static_cast<uint32_t>(fmt));
	cacore::hash_combine(hash, static_cast<uint32_t>(key.depthFormat));
	cacore::hash_combine(hash, key.hasDepth);

	auto it = m_RenderPassCache.find(hash);
	if (it != m_RenderPassCache.end())
		return it->second;

	castl::vector<vk::AttachmentDescription> attachments;
	castl::vector<vk::AttachmentReference> colorRefs;
	vk::AttachmentReference depthRef{};

	for (size_t i = 0; i < key.colorFormats.size(); ++i)
	{
		vk::AttachmentDescription attachment{};
		attachment.format = key.colorFormats[i];
		attachment.samples = vk::SampleCountFlagBits::e1;
		attachment.loadOp = vk::AttachmentLoadOp::eClear;
		attachment.storeOp = vk::AttachmentStoreOp::eStore;
		attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		attachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
		attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
		attachments.push_back(attachment);
		colorRefs.push_back({ static_cast<uint32_t>(i), vk::ImageLayout::eColorAttachmentOptimal });
	}

	if (key.hasDepth)
	{
		vk::AttachmentDescription depthAttachment{};
		depthAttachment.format = key.depthFormat;
		depthAttachment.samples = vk::SampleCountFlagBits::e1;
		depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		attachments.push_back(depthAttachment);
		depthRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
		depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
	}

	vk::SubpassDescription subpass{};
	subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
	subpass.pColorAttachments = colorRefs.data();
	if (key.hasDepth)
		subpass.pDepthStencilAttachment = &depthRef;

	vk::RenderPassCreateInfo renderPassInfo{};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	try
	{
		auto renderPass = m_Device.createRenderPass(renderPassInfo);
		m_RenderPassCache[hash] = renderPass;
		return renderPass;
	}
	catch (vk::SystemError const& e)
	{
		CA_LOG_ERR("RenderBackend_Vulkan: Failed to create render pass: {}", e.what());
		return nullptr;
	}
}

vk::Framebuffer RenderBackend_Vulkan::GetOrCreateFramebuffer(vk::RenderPass renderPass,
	castl::vector<vk::ImageView> const& attachments, uint32_t width, uint32_t height)
{
	size_t hash = 0;
	cacore::hash_combine(hash, reinterpret_cast<uintptr_t>(static_cast<VkRenderPass>(renderPass)));
	for (auto const& view : attachments)
		cacore::hash_combine(hash, reinterpret_cast<uintptr_t>(static_cast<VkImageView>(view)));
	cacore::hash_combine(hash, width);
	cacore::hash_combine(hash, height);

	auto it = m_FramebufferCache.find(hash);
	if (it != m_FramebufferCache.end())
		return it->second;

	vk::FramebufferCreateInfo framebufferInfo{};
	framebufferInfo.renderPass = renderPass;
	framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	framebufferInfo.pAttachments = attachments.data();
	framebufferInfo.width = width;
	framebufferInfo.height = height;
	framebufferInfo.layers = 1;

	try
	{
		auto framebuffer = m_Device.createFramebuffer(framebufferInfo);
		m_FramebufferCache[hash] = framebuffer;
		return framebuffer;
	}
	catch (vk::SystemError const& e)
	{
		CA_LOG_ERR("RenderBackend_Vulkan: Failed to create framebuffer: {}", e.what());
		return nullptr;
	}
}

}

CA_MODULE_INSTANCE(graphics_backend::CRenderBackend, graphics_backend::RenderBackend_Vulkan, RenderBackend_Vulkan);