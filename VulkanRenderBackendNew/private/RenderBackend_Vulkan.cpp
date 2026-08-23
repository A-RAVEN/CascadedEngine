#include <RenderBackend_Vulkan.h>
#include <Utils/VulkanDebug.h>
#include <string>
#include <cstring>
#include <cstdio>
#include <VulkanObjects/VulkanBuffer.h>
#include <VulkanObjects/VulkanTexture.h>
#include <VulkanObjects/VulkanWindowHandle.h>
#include <VulkanObjects/VulkanShaderStruct.h>

#include <windows.h>
#include <filesystem>

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

	static castl::vector<const char*> GetDeviceExtensionNames(bool includeGplLibraries)
	{
		castl::vector<const char*> extensions{
			// VK_KHR_MAINTENANCE_4 removed: promoted to Vulkan 1.3 core, and no code path
			// uses any maintenance4 command/feature. Requesting it is redundant dead config.
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		};
		// F1b: GPL extension names are requested ONLY when the selected device actually
		// exposes them — requesting an unsupported extension makes vkCreateDevice return
		// VK_ERROR_EXTENSION_NOT_PRESENT (hard init failure instead of monolithic fallback).
		if (includeGplLibraries)
		{
			extensions.push_back(VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
			extensions.push_back(VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME);
		}
		return extensions;
	}

	static bool DeviceHasExtension(vk::PhysicalDevice device, char const* extensionName)
	{
		auto deviceExtensions = device.enumerateDeviceExtensionProperties();
		for (auto const& ext : deviceExtensions)
		{
			if (strcmp(ext.extensionName, extensionName) == 0)
				return true;
		}
		return false;
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

				// Decision 3: Configure fallback source directory by deriving project root from DLL path.
				// This protects against CWD-dependent path resolution failures in ScanSourceDirectory.
				HMODULE hModule = GetModuleHandleA("VulkanRenderBackend.dll");
				if (hModule)
				{
					char dllPath[MAX_PATH];
					DWORD len = GetModuleFileNameA(hModule, dllPath, MAX_PATH);
					if (len > 0 && len < MAX_PATH)
					{
						std::filesystem::path p(dllPath);
						p = p.parent_path(); // strip DLL filename
						bool found = false;
						for (int i = 0; i < 10 && !p.empty() && p != p.root_path(); ++i)
						{
							if (std::filesystem::exists(p / "CAResources"))
							{
								m_ShaderImporter.SetSourceDirectory(p / "CAResources");
								CA_LOG("ShaderImporter_Vulkan: Fallback source directory set to {}", (p / "CAResources").string());
								found = true;
								break;
							}
							p = p.parent_path();
						}
						if (!found)
						{
							CA_LOG_WARN("ShaderImporter_Vulkan: Could not find CAResources/ by walking up from DLL path: {}", dllPath);
						}
					}
				}
				else
				{
					CA_LOG_WARN("ShaderImporter_Vulkan: GetModuleHandleA failed, no fallback source directory configured");
				}
			}
		}

		VULKAN_HPP_DEFAULT_DISPATCHER.init();

		vk::ApplicationInfo application_info(
			"Test Backend"
			, 1
			, "Test Engine"
			, 0
			, VULKAN_API_VERSION_IN_USE);

#ifndef NDEBUG
		const castl::vector<const char*> g_validationLayers{
			"VK_LAYER_KHRONOS_validation"
		};
#endif

		auto extensions = GetInstanceExtensionNames();
#ifndef NDEBUG
		vk::InstanceCreateInfo instance_info({}, &application_info, g_validationLayers, extensions);

		vk::DebugUtilsMessengerCreateInfoEXT debugUtilsExt{ {},
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
			| vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
			vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral
			| vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance
			| vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
			&debugUtilsMessengerCallback };
		instance_info.setPNext(&debugUtilsExt);
#else
		vk::InstanceCreateInfo instance_info({}, &application_info, {}, extensions);
#endif
		try
		{
			m_VulkanInstance = vk::createInstance(instance_info);
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("RenderBackend_Vulkan: Failed to create Vulkan instance: {}", e.what());
			m_Released = true;  // Init failed: destructor's Release() must not re-run cleanup
			return;
		}
		VULKAN_HPP_DEFAULT_DISPATCHER.init(m_VulkanInstance);
#ifndef NDEBUG
		m_DebugMessenger = m_VulkanInstance.createDebugUtilsMessengerEXT(debugUtilsExt);
#endif
		//Init Device
		auto physicalDevices = m_VulkanInstance.enumeratePhysicalDevices();
		if (physicalDevices.empty())
		{
			CA_LOG_ERR("RenderBackend_Vulkan: No Vulkan physical devices found");
			m_VulkanInstance.destroyDebugUtilsMessengerEXT(m_DebugMessenger);
			m_VulkanInstance.destroy();
			m_VulkanInstance = nullptr;
			m_Released = true;  // Init failed: destructor's Release() must not re-run cleanup
			return;
		}
		// F1a: capability-based device selection — prefer a device that exposes
		// VK_EXT_graphics_pipeline_library instead of blindly taking front().
		m_PhysicalDevice = physicalDevices.front();
		for (auto const& candidate : physicalDevices)
		{
			if (DeviceHasExtension(candidate, VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME))
			{
				m_PhysicalDevice = candidate;
				break;
			}
		}
		// F1b: only request GPL extensions on devices that expose them.
		bool gplExtensionAvailable = DeviceHasExtension(m_PhysicalDevice, VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME);
		InitSubObj(&m_QueueContext);
		auto deviceExts = GetDeviceExtensionNames(gplExtensionAvailable);
		QueueContext::QueueCreationInfo queueCreationInfo{};
		m_QueueContext.InitQueueCreationInfo(m_PhysicalDevice, queueCreationInfo);

		// Feature enablement MUST follow a vkGetPhysicalDeviceFeatures2 query — enabling a
		// feature the device does not support makes vkCreateDevice return
		// VK_ERROR_FEATURE_NOT_PRESENT (vkCreateDevice refpage). dynamicRendering is a
		// Vulkan 1.3 core feature (always supported at apiVersion 1.3), while
		// graphicsPipelineLibrary is a mandatory feature of VK_EXT_graphics_pipeline_library
		// (extension support implies feature support per spec Feature Requirements) — the
		// query is kept defensively and gates m_PipelineLibrarySupported. Feature-unsupported
		// devices fall back to the monolithic pipeline path (m_PipelineLibrarySupported=false).
		vk::PhysicalDeviceVulkan13Features vulkan13Features{};
		vk::PhysicalDeviceGraphicsPipelineLibraryFeaturesEXT gplFeatures{};
		gplFeatures.pNext = &vulkan13Features;
		{
			vk::PhysicalDeviceFeatures2 features2{};
			features2.pNext = &gplFeatures;
			m_PhysicalDevice.getFeatures2(&features2);
			m_PipelineLibrarySupported = (gplFeatures.graphicsPipelineLibrary == VK_TRUE);
			CA_LOG_INFO("RenderBackend_Vulkan: queried device features — graphicsPipelineLibrary={}, dynamicRendering={}",
				static_cast<bool>(gplFeatures.graphicsPipelineLibrary),
				static_cast<bool>(vulkan13Features.dynamicRendering));
		}

		vk::DeviceCreateInfo deviceCreateInfo({}, queueCreationInfo.queueCreateInfoList, {}, deviceExts);
		deviceCreateInfo.pNext = &gplFeatures;

		try
		{
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

		// Pipeline library support was determined above via vkGetPhysicalDeviceFeatures2
		// (graphicsPipelineLibrary feature query) — feature support, not extension
		// presence, gates the GPL path.

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
				try
				{
					m_PipelineCache = m_Device.createPipelineCache(cacheInfo);
				}
				catch (vk::SystemError const& e2)
				{
					CA_LOG_ERR("RenderBackend_Vulkan: Failed to create empty pipeline cache: {}", e2.what());
					m_PipelineCache = nullptr;
				}
			}
			CA_LOG_INFO("RenderBackend_Vulkan: Pipeline cache created (loaded {} bytes from disk)", cacheData.size());
		}

		// Init GPU Frame Manager
		InitSubObj(&m_GPUFrameManager);
		m_GPUFrameManager.Init();
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("RenderBackend_Vulkan: Init failed: {}", e.what());
			// Stepped cleanup in reverse initialization order (no full Release())
			if (m_PipelineCache) { m_Device.destroyPipelineCache(m_PipelineCache); m_PipelineCache = nullptr; }
			// G2/REL-2 (design D2 补遗): guard each subobject's Release() by whether it was
			// initialized (pApp set). On an early exception (e.g. vkCreateDevice throws) the later
			// subobjects were never InitSubObj'ed, so pApp is null and Release() would null-deref
			// via GetDevice() (see VulkanCommandListManager::Release / VulkanPipelineLibrary::Release).
			// GPU frame manager Release() is a safe no-op when m_FrameContexts is empty (never
			// initialized), so no GetApp() guard is needed for it — but it was previously omitted
			// entirely on this path, leaking the frame manager (REL-2).
			if (m_PipelineLibraryCache.GetApp()) m_PipelineLibraryCache.Release();
			if (m_PipelineLibrary.GetApp()) m_PipelineLibrary.Release();
			if (m_CommandListManager.GetApp()) m_CommandListManager.Release();
			if (m_MemoryManager.GetApp()) m_MemoryManager.Release();
			if (m_SamplerManager.GetApp()) m_SamplerManager.Release();
			if (m_DescriptorSetLayoutContainer.GetApp()) m_DescriptorSetLayoutContainer.Release();
			m_GPUFrameManager.Release();
			if (m_Device) { m_Device.destroy(); m_Device = nullptr; }
			if (m_DebugMessenger) { m_VulkanInstance.destroyDebugUtilsMessengerEXT(m_DebugMessenger); m_DebugMessenger = nullptr; }
			if (m_VulkanInstance) { m_VulkanInstance.destroy(); m_VulkanInstance = nullptr; }
			m_Released = true;  // Init failed: destructor's Release() must not re-run cleanup
			return;
		}

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
	RenderBackend_Vulkan::~RenderBackend_Vulkan()
	{
		// Mirror D3D12 (~RenderBackend_D3D12 calls Release()): without this destructor, Release()
		// is never called, so the backend's vk::Device/vk::Instance and any backend-owned window
		// surfaces are leaked. The module manager (CAModuleManager::~CAModuleManager) now destroys
		// module instances in REVERSE registration order, so VulkanRenderBackend (index 2) is
		// destroyed AFTER IMGUIContext (index 7): IMGUIContext's window handles are released while
		// the backend is still alive (device valid), and this destructor then destroys the
		// device/instance as the final teardown step (no device users remain). This is the fix that
		// eliminates the forward-order UAF (backend freed before IMGUIContext destroyed its handles,
		// whose CleanupSwapchain then deref'd the freed backend via GetDevice()).
		//
		// IMPORTANT: unlike D3D12, Vulkan's vk::* wrappers THROW vk::SystemError (e.g. a
		// device-lost from vkDeviceWaitIdle during teardown). A throw escaping a noexcept
		// destructor calls std::terminate -> abort. The destructor MUST swallow exceptions
		// (the backend is being torn down; there is no caller to propagate to).
		try
		{
			Release();
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("RenderBackend_Vulkan::~RenderBackend_Vulkan: Release() threw during teardown: {}", e.what());
		}
		catch (...)
		{
			CA_LOG_ERR("RenderBackend_Vulkan::~RenderBackend_Vulkan: Release() threw an unknown exception during teardown");
		}
	}

	void RenderBackend_Vulkan::Release()
	{
		// Backend-level idempotency guard (design D2): Release() may be reached from the
		// destructor, and on Init-failure the cleanup already ran. Prevents double-destroy and,
		// critically, prevents the WaitIdle() at the top from null-derefing an un-initialized GPU
		// frame manager (GetDevice() = pApp->GetVulkanDevice() with pApp unset) on the Init-fail
		// path.
		if (m_Released) return;
		m_Released = true;

		// D1: GPU idle BEFORE any device-object destruction — in-flight present/acquire
		// referencing swapchain imageViews (destroyed by window CleanupSwapchain) or
		// framebuffers would otherwise be a use-after-free during their destruction.
		// D6: guard GetApp() — m_GPUFrameManager may never have been InitSubObj'ed on a
		// pre-device Init-failure path (then pApp is null and GetDevice() derefs it → NULL DEREF).
		if (m_GPUFrameManager.GetApp())
		{
			// D2: vkDeviceWaitIdle returns VkResult, so it THROWS vk::SystemError on device-lost.
			// Per-step try/catch (NOT a single outer catch): m_Released is already true above, so an
			// outer catch would early-return on a second Release() and leave the window/device/instance
			// teardown half-done. Catch here so we still proceed to window cleanup + device destroy.
			try
			{
				m_GPUFrameManager.WaitIdle();
			}
			catch (vk::SystemError const& e)
			{
				CA_LOG_WARN("RenderBackend_Vulkan: Release() WaitIdle threw ({}); continuing", e.what());
			}
			catch (...)
			{
				CA_LOG_WARN("RenderBackend_Vulkan: Release() WaitIdle threw unknown; continuing");
			}
		}

		// Destroy framebuffers BEFORE window handles — framebuffers reference
		// swapchain image views owned by window handles; destroying windows first
		// would leave dangling view references in the framebuffers.
		{
			auto device = m_Device;
			for (auto& [hash, framebuffer] : m_FramebufferCache)
			{
				if (framebuffer) device.destroyFramebuffer(framebuffer);
			}
			m_FramebufferCache.clear();
		}

		// Release all window handles before destroying device/instance
		for (auto& [window, weakHandle] : m_WindowHandles)
		{
			if (auto handle = weakHandle.lock())
			{
				static_cast<VulkanWindowHandle*>(handle.get())->Release();
			}
		}
		m_WindowHandles.clear();

		// Release GPU Frame Manager (frame contexts: fences/semaphores/descriptor pool)
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
		// D6: guard GetApp() — m_PipelineLibrary may never have been InitSubObj'ed on a
		// pre-device Init-failure path (its Release() derefs pApp via GetDevice()).
		if (m_PipelineLibrary.GetApp())
			m_PipelineLibrary.Release();

		// Cleanup cross-frame caches (framebuffers already destroyed above)
		{
			auto device = m_Device;
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
		// D6: guard GetApp() — m_CommandListManager may not be initialized on pre-device Init-fail.
		if (m_CommandListManager.GetApp())
			m_CommandListManager.Release();

		// Release sampler manager
		// D6: guard GetApp() — m_SamplerManager may not be initialized on pre-device Init-fail.
		if (m_SamplerManager.GetApp())
			m_SamplerManager.Release();

		// Release memory manager
		m_MemoryManager.Release();

		// Release descriptor set layout container
		m_DescriptorSetLayoutContainer.Release();

		// Destroy device
		if (m_Device)
		{
			// D2: vkDeviceWaitIdle returns VkResult → can throw vk::SystemError on device-lost (e.g.
			// during teardown after a GPU fault). Per-step catch so device destroy still runs below
			// (an uncaught throw here would skip m_Device.destroy() → leak, and m_Released is already
			// true so a second Release() would early-return leaving it half-torn-down).
			try
			{
				m_Device.waitIdle();
			}
			catch (vk::SystemError const& e)
			{
				CA_LOG_WARN("RenderBackend_Vulkan: Release() device.waitIdle threw ({}); continuing", e.what());
			}
			catch (...)
			{
				CA_LOG_WARN("RenderBackend_Vulkan: Release() device.waitIdle threw unknown; continuing");
			}
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
	void RenderBackend_Vulkan::WaitIdle()
	{
		m_Device.waitIdle();
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
#ifndef NDEBUG
		std::string smName = "ShaderMod:" + std::to_string(reinterpret_cast<uintptr_t>(static_cast<VkShaderModule>(shaderModule)));
		SetVKObjectDebugName(m_Device, shaderModule, smName.c_str());
#endif
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
		hash = cacore::hash_combine(hash, setLayoutInfo.setIndex);
		for (auto const& binding : setLayoutInfo.bindings)
		{
			hash = cacore::hash_combine(hash, binding.descriptorType);
			hash = cacore::hash_combine(hash, binding.binding);
			hash = cacore::hash_combine(hash, binding.descriptorCount);
			hash = cacore::hash_combine(hash, binding.stageFlags);
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
#ifndef NDEBUG
		std::string plName = "PipelineLayout:" + std::to_string(hash);
		SetVKObjectDebugName(m_Device, pipelineLayout, plName.c_str());
#endif
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
		hash = cacore::hash_combine(hash, static_cast<uint32_t>(fmt));
	if (key.hasDepth)
		hash = cacore::hash_combine(hash, static_cast<uint32_t>(key.depthFormat));
	hash = cacore::hash_combine(hash, key.hasDepth);

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
#ifndef NDEBUG
		std::string rpName = "RenderPass:" + std::to_string(key.colorFormats.size()) + "c" + (key.hasDepth ? "+d" : "");
		SetVKObjectDebugName(m_Device, renderPass, rpName.c_str());
#endif
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
	hash = cacore::hash_combine(hash, reinterpret_cast<uintptr_t>(static_cast<VkRenderPass>(renderPass)));
	for (auto const& view : attachments)
		hash = cacore::hash_combine(hash, reinterpret_cast<uintptr_t>(static_cast<VkImageView>(view)));
	hash = cacore::hash_combine(hash, width);
	hash = cacore::hash_combine(hash, height);

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
#ifndef NDEBUG
		std::string fbName = "Framebuf:" + std::to_string(width) + "x" + std::to_string(height);
		SetVKObjectDebugName(m_Device, framebuffer, fbName.c_str());
#endif
		m_FramebufferCache[hash] = framebuffer;
		return framebuffer;
	}
	catch (vk::SystemError const& e)
	{
		CA_LOG_ERR("RenderBackend_Vulkan: Failed to create framebuffer: {}", e.what());
		return nullptr;
	}
}

void RenderBackend_Vulkan::ClearFramebufferCache()
{
	if (m_FramebufferCache.empty())
		return;
	for (auto& [hash, framebuffer] : m_FramebufferCache)
	{
		if (framebuffer) m_Device.destroyFramebuffer(framebuffer);
	}
	m_FramebufferCache.clear();
	CA_LOG_INFO("RenderBackend_Vulkan: Framebuffer cache cleared");
}

}

CA_MODULE_INSTANCE(graphics_backend::CRenderBackend, graphics_backend::RenderBackend_Vulkan, RenderBackend_Vulkan);
