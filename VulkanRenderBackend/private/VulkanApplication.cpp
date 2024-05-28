#include "pch.h"
#include <library_loader.h>
#include <FileLoader.h>
#include <MoveWrapper.h>
#include <CASTL/CASet.h>
#include "Utils.h"
#include "RenderBackendSettings.h"
#include "VulkanApplication.h"
#include "VulkanBarrierCollector.h"
#include "CommandList_Impl.h"
#include "InterfaceTranslator.h"
#include "GPUGraphExecutor/GPUGraphExecutor.h"
#include <GPUResources/VKGPUTexture.h>
#include <GPUResources/VKGPUBuffer.h>

namespace graphics_backend
{
	//void CVulkanApplication::SyncPresentationFrame(FrameType frameID)
	//{
	//	////Update Frame, Release FrameBound Resources
	//	//m_SubmitCounterContext.WaitingForCurrentFrame();
	//	//for (auto& windowContext : m_WindowContexts)
	//	//{
	//	//	windowContext->WaitCurrentFrameBufferIndex();
	//	//	windowContext->MarkUsages(ResourceUsage::eDontCare);
	//	//}

	//	//if (m_SubmitCounterContext.AnyFrameFinished())
	//	//{
	//	//	FrameType const releasedFrame = m_SubmitCounterContext.GetReleasedFrameID();
	//	//	TIndex const releasedIndex = m_SubmitCounterContext.GetReleasedResourcePoolIndex();
	//	//	for (auto itrThreadContext = m_ThreadContexts.begin(); itrThreadContext != m_ThreadContexts.end(); ++itrThreadContext)
	//	//	{
	//	//		itrThreadContext->DoReleaseContextResourceByIndex(releasedIndex);
	//	//	}
	//	//	GetGPUObjectManager().ReleaseFrameboundResources(releasedFrame);
	//	//	m_MemoryManager.ReleaseCurrentFrameResource(releasedFrame, releasedIndex);
	//	//	for (auto& windowContext : m_WindowContexts)
	//	//	{
	//	//		windowContext->TickReleaseResources(releasedFrame);
	//	//	}
	//	//}
	//}

	void CVulkanApplication::ScheduleGPUFrame(CTaskGraph* taskGraph, GPUFrame const& gpuFrame)
	{
		auto runGpuFrameTaskGraph = taskGraph->NewTaskGraph()
			->Name("Run GPU Frame")
			->SetupFunctor([this, gpuFrame](CTaskGraph* thisGraph)
			{
				auto frameManager =	m_FrameContext.GetFrameBoundResourceManager();
				frameManager->releaseQueue.Load(m_GlobalResourceReleasingQueue);
				for (auto& graph : gpuFrame.graphs)
				{
					castl::shared_ptr<GPUGraphExecutor> executor = NewSubObject_Shared<GPUGraphExecutor>(graph, frameManager);
					thisGraph->AddResource(executor);
					executor->PrepareGraph();
					frameManager->GetQueueContext();
				}
				if (gpuFrame.presentWindows.size() > 0)
				{
					for (auto& window : gpuFrame.presentWindows)
					{
						auto windowContext = castl::static_shared_pointer_cast<CWindowContext>(window);
						windowContext->PrepareForPresent();
						windowContext->PresentCurrentFrame();
					}
				}
			});
	}


	void CVulkanApplication::CreateImageViews2D(vk::Format format, castl::vector<vk::Image> const& inImages,
		castl::vector<vk::ImageView>& outImageViews) const
	{
		for(auto& img : inImages)
		{
			vk::ImageViewCreateInfo createInfo({}
				, img
				, vk::ImageViewType::e2D
				, format
				, vk::ComponentMapping(
					vk::ComponentSwizzle::eIdentity
					, vk::ComponentSwizzle::eIdentity
					, vk::ComponentSwizzle::eIdentity
					, vk::ComponentSwizzle::eIdentity)
				, vk::ImageSubresourceRange(
					vk::ImageAspectFlagBits::eColor
					, 0
					, 1
					, 0
					, 1));
			auto newView = GetDevice().createImageView(createInfo);
			outImageViews.push_back(newView);
		}
	}

	vk::ImageView CVulkanApplication::CreateDefaultImageView(
		GPUTextureDescriptor const& inDescriptor
		, vk::Image inImage
		, bool depthAspect
		, bool stencilAspect) const
	{
		bool isDepthStencil = IsDepthStencilFormat(inDescriptor.format);
		bool isDepthOnly = IsDepthOnlyFormat(inDescriptor.format);

		if ((depthAspect || stencilAspect) && !isDepthStencil)
			return nullptr;

		if(isDepthOnly && stencilAspect && !depthAspect)
			return nullptr;

		vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor;
		if (isDepthStencil)
		{
			aspectFlags = {};
			if (depthAspect)
			{
				aspectFlags |= vk::ImageAspectFlagBits::eDepth;
			}
			if (stencilAspect)
			{
				aspectFlags |= vk::ImageAspectFlagBits::eStencil;
			}
		}

		auto imageInfo = ETextureTypeToVulkanImageInfo(inDescriptor.textureType);
		vk::ImageViewCreateInfo createInfo{
			vk::ImageViewCreateFlags{}
			, inImage
			, imageInfo.defaultImageViewType
			, ETextureFormatToVkFotmat(inDescriptor.format)
			, vk::ComponentMapping(
				vk::ComponentSwizzle::eIdentity
				, vk::ComponentSwizzle::eIdentity
				, vk::ComponentSwizzle::eIdentity
				, vk::ComponentSwizzle::eIdentity)
				, vk::ImageSubresourceRange{
				aspectFlags
					, 0
					, VK_REMAINING_MIP_LEVELS
					, 0
					, VK_REMAINING_ARRAY_LAYERS}
		};
		return GetDevice().createImageView(createInfo);
	}

	GPUBuffer* CVulkanApplication::NewGPUBuffer(GPUBufferDescriptor const& inDescriptor)
	{
		VKGPUBuffer* result = new VKGPUBuffer(*this);
		result->SetDescriptor(inDescriptor);
		VKBufferObject bufferObject{};
		bufferObject.buffer = m_GPUResourceObjManager.CreateBuffer(inDescriptor);
		bufferObject.allocation = m_GPUMemoryManager.AllocateMemory(bufferObject.buffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
		result->SetBuffer(bufferObject);
		return result;
	}

	void CVulkanApplication::ReleaseGPUBuffer(GPUBuffer* releaseGPUBuffer)
	{
		VKGPUBuffer* vkBuffer = static_cast<VKGPUBuffer*>(releaseGPUBuffer);
		if (vkBuffer->Initialized())
		{
			m_GlobalResourceReleasingQueue.AddBuffers(vkBuffer->GetBuffer());
		}
	}

	GPUTexture* CVulkanApplication::NewGPUTexture(GPUTextureDescriptor const& inDescriptor)
	{
		VKGPUTexture* result = new VKGPUTexture(*this);
		result->SetDescriptor(inDescriptor);
		VKImageObject imageObject{};
		imageObject.image = m_GPUResourceObjManager.CreateImage(inDescriptor);
		imageObject.allocation = m_GPUMemoryManager.AllocateMemory(imageObject.image, vk::MemoryPropertyFlagBits::eDeviceLocal);
		result->SetImage(imageObject);
		return result;
	}

	void CVulkanApplication::ReleaseGPUTexture(GPUTexture* releaseGPUTexture)
	{
		VKGPUTexture* vkTexture = static_cast<VKGPUTexture*>(releaseGPUTexture);
		if (vkTexture->Initialized())
		{
			m_GlobalResourceReleasingQueue.AddImages(vkTexture->GetImage());
		}
	}

	castl::shared_ptr<ShaderConstantSet> CVulkanApplication::NewShaderConstantSet(ShaderConstantsBuilder const& builder)
	{
		auto subAllocator = m_ConstantSetAllocator.GetOrCreate(builder);
		return subAllocator->AllocateSet();
	}

	castl::shared_ptr<ShaderBindingSet> CVulkanApplication::NewShaderBindingSet(ShaderBindingBuilder const& builder)
	{
		auto subAllocator = m_ShaderBindingSetAllocator.GetOrCreate(builder);
		return subAllocator->AllocateSet();
	}

	void CVulkanApplication::InitializeInstance(castl::string const& name, castl::string const& engineName)
	{
		vk::ApplicationInfo application_info(
			name.c_str()
			, 1
			, engineName.c_str()
			, 0
			, VULKAN_API_VERSION_IN_USE);

		castl::array<const char*, 1> extensionNames = {
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME
		};

		const castl::vector<const char*> g_validationLayers{
			"VK_LAYER_KHRONOS_validation"
		};


		auto extensions = GetInstanceExtensionNames();
		vk::InstanceCreateInfo instance_info({}, &application_info, g_validationLayers, extensions);

#if !defined(NDEBUG)
		vk::DebugUtilsMessengerCreateInfoEXT debugUtilsExt = vulkan_backend::utils::makeDebugUtilsMessengerCreateInfoEXT();
		instance_info.setPNext(&debugUtilsExt);
#endif
		m_Instance = vk::createInstance(instance_info);

		vulkan_backend::utils::SetupVulkanInstanceFunctionPointers(m_Instance);
	#if !defined(NDEBUG)
		m_DebugMessager = m_Instance.createDebugUtilsMessengerEXT(debugUtilsExt);
	#endif
	}

	void CVulkanApplication::DestroyInstance()
	{
		if (m_Instance != vk::Instance(nullptr))
		{
	#if !defined( NDEBUG )
			m_Instance.destroyDebugUtilsMessengerEXT(m_DebugMessager);
			m_DebugMessager = nullptr;
	#endif

			vulkan_backend::utils::CleanupVulkanInstanceFuncitonPointers();
			m_Instance.destroy();
			m_Instance = nullptr;
		}
	}

	void CVulkanApplication::EnumeratePhysicalDevices()
	{
		m_PhysicalDevice = m_Instance.enumeratePhysicalDevices().front();
		std::cout << "Device Name: " << m_PhysicalDevice.getProperties().deviceName << std::endl;
	}

	void CVulkanApplication::CreateDevice()
	{
		std::vector<vk::QueueFamilyProperties> queueFamilyProperties = m_PhysicalDevice.getQueueFamilyProperties();
		castl::vector<castl::pair<uint32_t, uint32_t>> generalUsageQueues;
		castl::vector<castl::pair<uint32_t, uint32_t>> computeDedicateQueues;
		castl::vector<castl::pair<uint32_t, uint32_t>> transferDedicateQueues;

		vk::QueueFlags generalFlags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;
		for (uint32_t queueId = 0; queueId < queueFamilyProperties.size(); ++queueId)
		{
			vk::QueueFamilyProperties const& itrProp = queueFamilyProperties[queueId];
			if ((itrProp.queueFlags & generalFlags) == generalFlags)
			{
				generalUsageQueues.push_back(castl::make_pair(queueId, itrProp.queueCount));
			}
			else
			{
				if (itrProp.queueFlags & vk::QueueFlagBits::eCompute)
				{
					computeDedicateQueues.push_back(castl::make_pair(queueId, itrProp.queueCount));
				}
				else if (itrProp.queueFlags & vk::QueueFlagBits::eTransfer)
				{
					transferDedicateQueues.push_back(castl::make_pair(queueId, itrProp.queueCount));
				}
				else
				{
				}
			}
		}

		CA_ASSERT(!generalUsageQueues.empty(), "Vulkan: No General Usage Queue Found!");

		if (computeDedicateQueues.empty())
		{
			computeDedicateQueues.push_back(generalUsageQueues[0]);
		}
		if (transferDedicateQueues.empty())
		{
			transferDedicateQueues.push_back(generalUsageQueues[0]);
		}
		castl::set<castl::pair<uint32_t, uint32_t>> queueFamityIndices;
		queueFamityIndices.insert(generalUsageQueues[0]);
		queueFamityIndices.insert(computeDedicateQueues[0]);
		queueFamityIndices.insert(transferDedicateQueues[0]);

		castl::vector<vk::DeviceQueueCreateInfo> deviceQueueCreateInfoList;
		castl::pair<uint32_t, uint32_t> generalQueueRef;
		castl::pair<uint32_t, uint32_t> computeQueueRef;
		castl::pair<uint32_t, uint32_t> transferQueueRef;
		castl::vector<castl::vector<float>> queuePriorities;
		for (castl::pair<uint32_t, uint32_t> const& itrQueueInfo : queueFamityIndices)
		{
			uint32_t queueFamilyId = itrQueueInfo.first;
			uint32_t queueCount = itrQueueInfo.second;

			uint32_t itrQueueId = 0;
			uint32_t requiredQueueCount = 0;
			if (queueFamilyId == generalUsageQueues[0].first)
			{
				generalQueueRef = castl::make_pair(queueFamilyId, itrQueueId);
				itrQueueId = (itrQueueId + 1) % queueCount;
				requiredQueueCount = castl::min(requiredQueueCount + 1, queueCount);
			}
			if (queueFamilyId == computeDedicateQueues[0].first)
			{
				computeQueueRef = castl::make_pair(queueFamilyId, itrQueueId);
				itrQueueId = (itrQueueId + 1) % queueCount;
				requiredQueueCount = castl::min(requiredQueueCount + 1, queueCount);
			}

			if (queueFamilyId == transferDedicateQueues[0].first)
			{
				transferQueueRef = castl::make_pair(queueFamilyId, itrQueueId);
				itrQueueId = (itrQueueId + 1) % queueCount;
				requiredQueueCount = castl::min(requiredQueueCount + 1, queueCount);
			}

			{
				castl::vector<float> tmp;
				tmp.resize(requiredQueueCount);
				castl::fill(tmp.begin(), tmp.end(), 0.0f);
				queuePriorities.push_back(tmp);
			}
			auto& currentQueuePriorities = queuePriorities.back();
			deviceQueueCreateInfoList.emplace_back(vk::DeviceQueueCreateFlags(), queueFamilyId, currentQueuePriorities);
		}

		castl::vector<uint32_t> otherQueueFamilies;
		for(uint32_t familyId = 0; familyId < queueFamilyProperties.size(); ++familyId)
		{
			if(familyId != generalQueueRef.first && familyId != computeQueueRef.first && familyId != transferQueueRef.first)
			{
				otherQueueFamilies.push_back(familyId);
			}
		}


		for(uint32_t itrOtherFamilyId : otherQueueFamilies)
		{
			queuePriorities.push_back({ 0.0f });
			auto& currentQueuePriorities = queuePriorities.back();
			deviceQueueCreateInfoList.emplace_back(vk::DeviceQueueCreateFlags(), itrOtherFamilyId, currentQueuePriorities);
		}
		QueueContext::QueueCreationInfo queueCreationInfo{};
		m_QueueContext.InitQueueCreationInfo(m_PhysicalDevice, queueCreationInfo);
		auto extensions = GetDeviceExtensionNames();
		vk::DeviceCreateInfo deviceCreateInfo({}, queueCreationInfo.queueCreateInfoList, {}, extensions);
		m_Device = m_PhysicalDevice.createDevice(deviceCreateInfo);

		castl::vector<vk::Queue> defaultQueues;
		defaultQueues.reserve(queueFamilyProperties.size());
		for(int itrFamily = 0; itrFamily < queueFamilyProperties.size(); ++itrFamily)
		{
			vk::Queue itrQueue = m_Device.getQueue(itrFamily, 0);
			defaultQueues.push_back(itrQueue);
		}

		m_SubmitCounterContext.Initialize();
		m_SubmitCounterContext.InitializeSubmitQueues(generalQueueRef, computeQueueRef, transferQueueRef);
		m_SubmitCounterContext.InitializeDefaultQueues(defaultQueues);

		vulkan_backend::utils::SetupVulkanDeviceFunctinoPointers(m_Device);
	}

	void CVulkanApplication::DestroyDevice()
	{
		m_SubmitCounterContext.Release();
		if(m_Device != vk::Device(nullptr))
		{
			m_Device.destroy();
			m_Device = nullptr;
		}
	}

	//void CVulkanApplication::InitializeThreadContext(uint32_t threadCount)
	//{
	//	CA_ASSERT(threadCount > 0, "Thread Count Should Be Greater Than 0");
	//	//CA_ASSERT(m_ThreadContexts.size() == 0, "Thread Contexts Are Already Initialized");
	//	//m_ThreadContexts.reserve(threadCount);
	//	//for (uint32_t threadContextId = 0; threadContextId < threadCount; ++threadContextId)
	//	//{
	//	//	m_ThreadContexts.push_back(castl::move(NewSubObject<CVulkanThreadContext>(threadContextId)));
	//	//}
	//	//castl::vector<uint32_t> threadInitializeValue;
	//	//threadInitializeValue.resize(threadCount);
	//	//uint32_t id = 0;
	//	//castl::generate(threadInitializeValue.begin(), threadInitializeValue.end(), [&id]()
	//	//	{
	//	//		return id++;
	//	//	});
	//	//m_AvailableThreadQueue.Initialize(threadInitializeValue);
	//}

	/*void CVulkanApplication::DestroyThreadContexts()
	{
		for (auto& threadContext : m_ThreadContexts)
		{
			threadContext.Release();
		}
		m_ThreadContexts.clear();
	}*/

	/*CVulkanMemoryManager& CVulkanApplication::GetMemoryManager()
	{
		return m_MemoryManager;
	}*/

	//CVulkanThreadContext& CVulkanApplication::AquireThreadContext()
	//{
	//	uint32_t available = m_AvailableThreadQueue.TryGetFront();
	//	return m_ThreadContexts[available];
	//}

	//void CVulkanApplication::ReturnThreadContext(CVulkanThreadContext& returningContext)
	//{
	//	uint32_t id = returningContext.GetThreadID();
	//	m_AvailableThreadQueue.Enqueue(id);
	//}

	//castl::shared_ptr<CVulkanThreadContext> CVulkanApplication::AquireThreadContextPtr()
	//{
	//	return castl::shared_ptr<CVulkanThreadContext>(&AquireThreadContext(), [this](CVulkanThreadContext* releasingContext)
	//		{
	//			ReturnThreadContext(*releasingContext);
	//		});
	//}

	castl::shared_ptr<WindowHandle> CVulkanApplication::CreateWindowContext(castl::string windowName, uint32_t initialWidth, uint32_t initialHeight
		, bool visible
		, bool focused
		, bool decorate
		, bool floating)
	{
		m_WindowContexts.emplace_back(castl::make_shared<CWindowContext>(*this));
		auto newContext = m_WindowContexts.back();
		newContext->Initialize(windowName, initialWidth, initialHeight
			, visible
			, focused
			, decorate
			, floating);
		return newContext;
	}

	void CVulkanApplication::TickWindowContexts()
	{
		FrameType currentFrameID = m_SubmitCounterContext.GetCurrentFrameID();
		CWindowContext::UpdateMonitors();
		//CA_LOG_ERR(castl::string("windows count ") + castl::to_string(m_WindowContexts.size()));
		
		glfwPollEvents();
		//glfwWaitEvents();
		for (auto& windowContext : m_WindowContexts)
		{
			windowContext->UpdatePos();
			windowContext->UpdateSize();
		}
		bool anyNeedClose = castl::any_of(m_WindowContexts.begin(), m_WindowContexts.end(), [](auto& wcontest)
			{
				return wcontest->NeedClose();
			});
		bool anyResized = castl::any_of(m_WindowContexts.begin(), m_WindowContexts.end(), [](auto& wcontest)
			{
				return wcontest->Resized();
			});
		if (anyNeedClose || anyResized)
		{
			DeviceWaitIdle();
		}
		if(anyNeedClose)
		{
			size_t lastIndex = m_WindowContexts.size();
			size_t currentIndex = 0;
			while (currentIndex < m_WindowContexts.size())
			{
				if (m_WindowContexts[currentIndex]->NeedClose())
				{
					m_WindowContexts[currentIndex]->Release();
					castl::swap(m_WindowContexts[currentIndex], m_WindowContexts.back());
					m_WindowContexts.pop_back();
				}
				else
				{
					++currentIndex;
				}
			}
		}
		if (anyResized)
		{
			for (auto& windowContext : m_WindowContexts)
			{
				windowContext->Resize(currentFrameID);
			}
		}
	}

	void CVulkanApplication::ReleaseAllWindowContexts()
	{
		m_WindowContexts.clear();
	}

	CVulkanApplication::CVulkanApplication() :
	//m_GPUBufferPool(*this)
	//, m_GPUTexturePool(*this)
	m_GPUObjectManager(*this)
	//, m_MemoryManager(*this)
	//, m_RenderGraphDic(*this)
	, m_ConstantSetAllocator(*this)
	, m_ShaderBindingSetAllocator(*this)
	, m_SubmitCounterContext(*this)
	, m_QueueContext(*this)
	, m_FrameContext(*this)
	, m_GPUResourceObjManager(*this)
	, m_GPUMemoryManager(*this)
	, m_GlobalResourceReleasingQueue(*this)
	{
	}

	CVulkanApplication::~CVulkanApplication()
	{
		//ReleaseApp();
	}

	void CVulkanApplication::InitApp(castl::string const& appName, castl::string const& engineName)
	{
		InitializeInstance(appName, engineName);
		EnumeratePhysicalDevices();
		CreateDevice();
		m_GPUMemoryManager.Initialize();
		m_GPUResourceObjManager.Initialize();
		m_FrameContext.InitFrameCapacity(4);

	}

	void CVulkanApplication::ReleaseApp()
	{
		DeviceWaitIdle();
		//m_ConstantSetAllocator.Release();
		//m_ShaderBindingSetAllocator.Release();
		//m_GPUBufferPool.ReleaseAll();
		//m_GPUTexturePool.ReleaseAll();
		//m_MemoryManager.Release();
		//DestroyThreadContexts();
		ReleaseAllWindowContexts();
		m_FrameContext.Release();
		m_GPUResourceObjManager.Release();
		m_GPUMemoryManager.Release();
		DestroyDevice();
		m_PhysicalDevice = nullptr;
		DestroyInstance();
	}

	void CVulkanApplication::DeviceWaitIdle()
	{
		//m_TaskFuture.wait();
		if (m_Device != vk::Device(nullptr))
		{
			m_Device.waitIdle();
		}
	}

}
