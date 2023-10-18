#include "pch.h"
#include "Utils.h"
#include "RenderBackendSettings.h"
#include "CVulkanApplication.h"
#include "CVulkanThreadContext.h"
#include "CVulkanBufferObject.h"
#include "VulkanBarrierCollector.h"
#include <ShaderCompiler/header/Compiler.h>
#include <SharedTools/header/library_loader.h>
#include <SharedTools/header/FileLoader.h>
#include "CommandList_Impl.h"
#include "InterfaceTranslator.h"
#include "RenderGraphExecutor.h"
#include <SharedTools/header/MoveWrapper.h>

namespace graphics_backend
{
	void CVulkanApplication::PrepareBeforeTick()
	{
		p_TaskGraph = p_ThreadManager->NewTaskGraph();
		p_TaskGraph->Name("Base GPU Task Graph");

		auto initializeTask = p_TaskGraph->NewTask()
			->Name("GPU Frame Initialize")
			->Functor([this]()
				{
					m_SubmitCounterContext.WaitingForCurrentFrame();
					if (m_SubmitCounterContext.AnyFrameFinished())
					{
						FrameType const releasedFrame = m_SubmitCounterContext.GetReleasedFrameID();
						for (auto itrThreadContext = m_ThreadContexts.begin(); itrThreadContext != m_ThreadContexts.end(); ++itrThreadContext)
						{
							itrThreadContext->DoReleaseResourceBeforeFrame(releasedFrame);
						}
						GetGPUObjectManager().ReleaseFrameboundResources(releasedFrame);
					}
					m_MemoryManager.ReleaseCurrentFrameResource();
					for (auto& windowContext : m_WindowContexts)
					{
						windowContext->WaitCurrentFrameBufferIndex();
						windowContext->MarkUsages(ResourceUsage::eDontCare);
					}
				});

		p_MemoryResourceUploadingTaskGraph = p_TaskGraph->NewTaskGraph()
			->Name("Memory Resource Uploading Task Graph")
			->DependsOn(initializeTask);

		p_GPUAddressUploadingTaskGraph = p_TaskGraph->NewTaskGraph()
			->Name("GPU Address Updating Task Graph")
			->DependsOn(p_MemoryResourceUploadingTaskGraph);

		p_RenderingTaskGraph = p_TaskGraph->NewTaskGraph()
			->Name("Rendering Task Graph")
			->DependsOn(p_GPUAddressUploadingTaskGraph);

		p_FinalizeTaskGraph = p_TaskGraph->NewTaskGraph()
			->Name("Finalize Task Graph")
			->DependsOn(p_RenderingTaskGraph);

		//Tick uploading shader bindings
		m_ShaderBindingSetAllocator.Foreach([this](ShaderBindingBuilder const&, ShaderBindingSetAllocator* allocator)
			{
				allocator->TickUploadResources(p_GPUAddressUploadingTaskGraph);
			});

	}

	void CVulkanApplication::EndThisFrame()
	{
		//auto executors = shared_tools::makeMoveWrapper(m_Executors);
		p_FinalizeTaskGraph->NewTask()
			->Name("Finalize")
			->Functor([this, executors = shared_tools::makeMoveWrapper(m_Executors)]()
				{
					//收集 Misc Commandbuffers
					std::vector<vk::CommandBuffer> waitingSubmitCommands;
					for (auto itrThreadContext = m_ThreadContexts.begin(); itrThreadContext != m_ThreadContexts.end(); ++itrThreadContext)
					{
						itrThreadContext->CollectSubmittingCommandBuffers(waitingSubmitCommands);
					}

					for (RenderGraphExecutor const& executor : *executors)
					{
						executor.CollectCommands(waitingSubmitCommands);
					}

					if (!m_WindowContexts.empty())
					{
						auto windowContext = m_WindowContexts[0];
						TIndex currentBuffer = windowContext->GetCurrentFrameBufferIndex();

						std::array<const vk::Semaphore, 1> semaphore = { windowContext->m_WaitNextFrameSemaphore };
						std::array<const vk::PipelineStageFlags, 1> waitStages = { vk::PipelineStageFlagBits::eTransfer };
						std::array<const vk::Semaphore, 1> presentSemaphore = { windowContext->m_CanPresentSemaphore };

						auto& threadContext = AquireThreadContext();
						auto cmd = threadContext.GetCurrentFramePool().AllocateOnetimeCommandBuffer();

						VulkanBarrierCollector presentBarrier{ GetSubmitCounterContext().GetGraphicsQueueFamily() };
						presentBarrier.PushImageBarrier(windowContext->GetCurrentFrameImage()
							, windowContext->m_CurrentFrameUsageFlags, ResourceUsage::ePresent);
						presentBarrier.ExecuteBarrier(cmd);

						cmd.end();
						waitingSubmitCommands.push_back(cmd);

						m_SubmitCounterContext.FinalizeCurrentFrameGraphics(waitingSubmitCommands
							, semaphore
							, waitStages
							, presentSemaphore);

						vk::PresentInfoKHR presenttInfo(
							presentSemaphore
							, windowContext->m_Swapchain
							, currentBuffer
						);
						windowContext->m_PresentQueue.second.presentKHR(presenttInfo);

						ReturnThreadContext(threadContext);
					}
					else
					{
						m_SubmitCounterContext.FinalizeCurrentFrameGraphics(waitingSubmitCommands);
					}
				});

		m_Executors.clear();

		if (m_TaskFuture.valid())
		{
			m_TaskFuture.wait();
		}

		m_TaskFuture = p_TaskGraph->Run();
	}

	void CVulkanApplication::ExecuteRenderGraph(std::shared_ptr<CRenderGraph> inRenderGraph)
	{
		auto rendergraphExcutionTaskGraph = p_RenderingTaskGraph->NewTaskGraph();
		m_Executors.push_back(std::move(RenderGraphExecutor{ *this }));
		RenderGraphExecutor& newExecutor = m_Executors.back();
		rendergraphExcutionTaskGraph->SetupFunctor([this, inRenderGraph, &newExecutor](CTaskGraph* thisGraph)
			{
				newExecutor.Create(inRenderGraph);
				newExecutor.Run(thisGraph);
			});
	}

	void CVulkanApplication::CreateImageViews2D(vk::Format format, std::vector<vk::Image> const& inImages,
		std::vector<vk::ImageView>& outImageViews) const
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

	GPUBuffer* CVulkanApplication::NewGPUBuffer(EBufferUsageFlags usageFlags, uint64_t count, uint64_t stride)
	{
		GPUBuffer_Impl* result = m_GPUBufferPool.Alloc(usageFlags, count, stride);
		return result;
	}

	void CVulkanApplication::ReleaseGPUBuffer(GPUBuffer* releaseGPUBuffer)
	{
		m_GPUBufferPool.Release(static_cast<GPUBuffer_Impl*>(releaseGPUBuffer));
	}

	GPUTexture* CVulkanApplication::NewGPUTexture(GPUTextureDescriptor const& inDescriptor)
	{
		return m_GPUTexturePool.Alloc(inDescriptor);
	}

	void CVulkanApplication::ReleaseGPUTexture(GPUTexture* releaseGPUTexture)
	{
		m_GPUTexturePool.Release(static_cast<GPUTexture_Impl*>(releaseGPUTexture));
	}

	std::shared_ptr<ShaderConstantSet> CVulkanApplication::NewShaderConstantSet(ShaderConstantsBuilder const& builder)
	{
		auto subAllocator = m_ConstantSetAllocator.GetOrCreate(builder).lock();
		return subAllocator->AllocateSet();
	}

	std::shared_ptr<ShaderBindingSet> CVulkanApplication::NewShaderBindingSet(ShaderBindingBuilder const& builder)
	{
		auto subAllocator = m_ShaderBindingSetAllocator.GetOrCreate(builder).lock();
		return subAllocator->AllocateSet();
	}

	std::shared_ptr<TextureSampler> CVulkanApplication::GetOrCreateTextureSampler(TextureSamplerDescriptor const& descriptor)
	{
		return m_GPUObjectManager.GetTextureSamplerCache().GetOrCreate(descriptor).lock();
	}

	void CVulkanApplication::InitializeInstance(std::string const& name, std::string const& engineName)
	{
		vk::ApplicationInfo application_info(
			name.c_str()
			, 1
			, engineName.c_str()
			, 0
			, VULKAN_API_VERSION_IN_USE);

		std::array<const char*, 1> extensionNames = {
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME
		};

		const std::vector<const char*> g_validationLayers{
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
	}

	void CVulkanApplication::CreateDevice()
	{
		std::vector<vk::QueueFamilyProperties> queueFamilyProperties = m_PhysicalDevice.getQueueFamilyProperties();

		std::vector<std::pair<uint32_t, uint32_t>> generalUsageQueues;
		std::vector<std::pair<uint32_t, uint32_t>> computeDedicateQueues;
		std::vector<std::pair<uint32_t, uint32_t>> transferDedicateQueues;

		vk::QueueFlags generalFlags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;
		for (uint32_t queueId = 0; queueId < queueFamilyProperties.size(); ++queueId)
		{
			vk::QueueFamilyProperties const& itrProp = queueFamilyProperties[queueId];
			if ((itrProp.queueFlags & generalFlags) == generalFlags)
			{
				generalUsageQueues.push_back(std::make_pair(queueId, itrProp.queueCount));
			}
			else
			{
				if (itrProp.queueFlags & vk::QueueFlagBits::eCompute)
				{
					computeDedicateQueues.push_back(std::make_pair(queueId, itrProp.queueCount));
				}
				else if (itrProp.queueFlags & vk::QueueFlagBits::eTransfer)
				{
					transferDedicateQueues.push_back(std::make_pair(queueId, itrProp.queueCount));
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
		std::set<std::pair<uint32_t, uint32_t>> queueFamityIndices;
		queueFamityIndices.insert(generalUsageQueues[0]);
		queueFamityIndices.insert(computeDedicateQueues[0]);
		queueFamityIndices.insert(transferDedicateQueues[0]);

		std::vector<vk::DeviceQueueCreateInfo> deviceQueueCreateInfoList;
		std::pair<uint32_t, uint32_t> generalQueueRef;
		std::pair<uint32_t, uint32_t> computeQueueRef;
		std::pair<uint32_t, uint32_t> transferQueueRef;
		std::vector<std::vector<float>> queuePriorities;
		for (std::pair<uint32_t, uint32_t> const& itrQueueInfo : queueFamityIndices)
		{
			uint32_t queueFamilyId = itrQueueInfo.first;
			uint32_t queueCount = itrQueueInfo.second;

			uint32_t itrQueueId = 0;
			uint32_t requiredQueueCount = 0;
			if (queueFamilyId == generalUsageQueues[0].first)
			{
				generalQueueRef = std::make_pair(queueFamilyId, itrQueueId);
				itrQueueId = (itrQueueId + 1) % queueCount;
				requiredQueueCount = std::min(requiredQueueCount + 1, queueCount);
			}
			if (queueFamilyId == computeDedicateQueues[0].first)
			{
				computeQueueRef = std::make_pair(queueFamilyId, itrQueueId);
				itrQueueId = (itrQueueId + 1) % queueCount;
				requiredQueueCount = std::min(requiredQueueCount + 1, queueCount);
			}

			if (queueFamilyId == transferDedicateQueues[0].first)
			{
				transferQueueRef = std::make_pair(queueFamilyId, itrQueueId);
				itrQueueId = (itrQueueId + 1) % queueCount;
				requiredQueueCount = std::min(requiredQueueCount + 1, queueCount);
			}

			{
				std::vector<float> tmp;
				tmp.resize(requiredQueueCount);
				std::fill(tmp.begin(), tmp.end(), 0.0f);
				queuePriorities.push_back(tmp);
			}
			auto& currentQueuePriorities = queuePriorities.back();
			deviceQueueCreateInfoList.emplace_back(vk::DeviceQueueCreateFlags(), queueFamilyId, currentQueuePriorities);
		}

		std::vector<uint32_t> otherQueueFamilies;
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

		auto extensions = GetDeviceExtensionNames();
		vk::DeviceCreateInfo deviceCreateInfo({}, deviceQueueCreateInfoList, {}, extensions);

		m_Device = m_PhysicalDevice.createDevice(deviceCreateInfo);

		std::vector<vk::Queue> defaultQueues;
		defaultQueues.reserve(queueFamilyProperties.size());
		for(int itrFamily = 0; itrFamily < queueFamilyProperties.size(); ++itrFamily)
		{
			vk::Queue itrQueue = m_Device.getQueue(itrFamily, 0);
			defaultQueues.push_back(itrQueue);
		}

		m_SubmitCounterContext.Initialize(this);
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

	void CVulkanApplication::InitializeThreadContext(CThreadManager* threadManager, uint32_t threadCount)
	{
		CA_ASSERT(threadCount > 0, "Thread Count Should Be Greater Than 0");
		CA_ASSERT(m_ThreadContexts.size() == 0, "Thread Contexts Are Already Initialized");
		p_ThreadManager = threadManager;
		m_ThreadContexts.reserve(threadCount);
		for (uint32_t threadContextId = 0; threadContextId < threadCount; ++threadContextId)
		{
			m_ThreadContexts.push_back(SubObject<CVulkanThreadContext>(threadContextId));
		}
		std::vector<uint32_t> threadInitializeValue;
		threadInitializeValue.resize(threadCount);
		uint32_t id = 0;
		std::generate(threadInitializeValue.begin(), threadInitializeValue.end(), [&id]()
			{
				return id++;
			});
		m_AvailableThreadQueue.Initialize(threadInitializeValue);
	}

	void CVulkanApplication::DestroyThreadContexts()
	{
		for (auto& threadContext : m_ThreadContexts)
		{
			ReleaseSubObject(threadContext);
		}
		m_ThreadContexts.clear();
	}

	CVulkanMemoryManager& CVulkanApplication::GetMemoryManager()
	{
		return m_MemoryManager;
	}

	CVulkanThreadContext& CVulkanApplication::AquireThreadContext()
	{
		uint32_t available = m_AvailableThreadQueue.TryGetFront();
		return m_ThreadContexts[available];
	}

	void CVulkanApplication::ReturnThreadContext(CVulkanThreadContext& returningContext)
	{
		uint32_t id = returningContext.GetThreadID();
		m_AvailableThreadQueue.Enqueue(id);
	}

	std::shared_ptr<CVulkanThreadContext> CVulkanApplication::AquireThreadContextPtr()
	{
		return std::shared_ptr<CVulkanThreadContext>(&AquireThreadContext(), [this](CVulkanThreadContext* releasingContext)
			{
				ReturnThreadContext(*releasingContext);
			});
	}

	CThreadManager* CVulkanApplication::GetThreadManager() const
	{
		return p_ThreadManager;
	}

	CTask* CVulkanApplication::NewTask()
	{
		return p_RenderingTaskGraph->NewTask();
	}

	TaskParallelFor* CVulkanApplication::NewTaskParallelFor()
	{
		return p_RenderingTaskGraph->NewTaskParallelFor();
	}

	CTask* CVulkanApplication::NewUploadingTask(UploadingResourceType resourceType)
	{
		switch(resourceType)
		{
		case UploadingResourceType::eMemoryDataThisFrame:
		case UploadingResourceType::eMemoryDataLowPriority:
			return p_MemoryResourceUploadingTaskGraph->NewTask();
		case UploadingResourceType::eAddressDataThisFrame:
			return p_GPUAddressUploadingTaskGraph->NewTask();
		}
		return nullptr;
	}

	std::shared_ptr<WindowHandle> CVulkanApplication::CreateWindowContext(std::string windowName, uint32_t initialWidth, uint32_t initialHeight)
	{
		m_WindowContexts.emplace_back(std::make_shared<CWindowContext>(*this));
		auto newContext = m_WindowContexts.back();
		newContext->Initialize(windowName, initialWidth, initialHeight);
		return newContext;
	}

	void CVulkanApplication::TickWindowContexts()
	{
		bool anyNeedClose = std::any_of(m_WindowContexts.begin(), m_WindowContexts.end(), [](auto& wcontest)
			{
				return wcontest->NeedClose();
			});
		if(anyNeedClose)
		{
			DeviceWaitIdle();
			size_t lastIndex = m_WindowContexts.size();
			size_t currentIndex = 0;
			while (currentIndex < m_WindowContexts.size())
			{
				if (m_WindowContexts[currentIndex]->NeedClose())
				{
					m_WindowContexts[currentIndex]->Release();
					std::swap(m_WindowContexts[currentIndex], m_WindowContexts.back());
					m_WindowContexts.pop_back();
				}
				else
				{
					++currentIndex;
				}
			}
		}
		glfwPollEvents();
	}

	void CVulkanApplication::ReleaseAllWindowContexts()
	{
		m_WindowContexts.clear();
	}

	CVulkanApplication::CVulkanApplication() :
	m_GPUBufferPool(*this)
	, m_GPUTexturePool(*this)
	, m_GPUObjectManager(*this)
	, m_MemoryManager(*this)
	, m_RenderGraphDic(*this)
	, m_ConstantSetAllocator(*this)
	, m_ShaderBindingSetAllocator(*this)
	{
	}

	CVulkanApplication::~CVulkanApplication()
	{
		//ReleaseApp();
	}

	void CVulkanApplication::InitApp(std::string const& appName, std::string const& engineName)
	{
		InitializeInstance(appName, engineName);
		EnumeratePhysicalDevices();
		CreateDevice();
		m_MemoryManager.Initialize();
	}

	void CVulkanApplication::ReleaseApp()
	{
		DeviceWaitIdle();
		m_ConstantSetAllocator.Release();
		m_ShaderBindingSetAllocator.Release();
		m_GPUBufferPool.ReleaseAll();
		m_GPUTexturePool.ReleaseAll();
		m_MemoryManager.Release();
		DestroyThreadContexts();
		ReleaseAllWindowContexts();
		DestroyDevice();
		m_PhysicalDevice = nullptr;
		DestroyInstance();
	}

	void CVulkanApplication::DeviceWaitIdle()
	{
		m_TaskFuture.wait();
		if (m_Device != vk::Device(nullptr))
		{
			m_Device.waitIdle();
		}
	}

}
