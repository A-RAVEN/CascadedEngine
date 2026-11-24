#include "D3D12Includes.h"
#include "D3D12Debug.h"
#include "RenderBackend_D3D12.h"
#include "WebRenderSpike/D3D12WebRenderSpike.h"
#include <CATimer/Timer.h>
#include <ResourceManagment/D3DImageObject.h>
#include <ResourceManagment/D3DBufferObject.h>
#include <ResourceManagment/GPUResourceStates.h>
#include <Utils/InterfaceTranslation.h>
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <GPUGraph/GPUGraphExecutor.h>
#include <GPUGraph/D3D12GPUTiming.h>
#include <chrono>
#include <cstdlib>
#include <thread>
#define CA_IMPLEMENT_MODULE 1
#include <CACore/CAModuleImplementation.h>

namespace graphics_backend
{
	namespace
	{
		// editor-web-render-spike 2.1（修订）：渲染原型的非绑定面实例。D3D12 模块每个进程只有一个
		// CRenderBackend 实例（`CA_MODULE_INSTANCE` 只登记一次），所以登记状态放文件作用域静态量，
		// **不去改被 CodeGen 解析的 `RenderBackend_D3D12.h`**（往那里加 include/继承会改 CodeGen 的
		// 解析形态，实测直接让保真守卫拒绝整轮生成）。
		std::unique_ptr<D3D12WebRenderSpikeProvider> g_WebRenderSpikeProvider;
		cacore::IModuleManager* g_WebRenderSpikeModuleManager = nullptr;
	}

	void GetHardwareAdapter(
		IDXGIFactory1* pFactory,
		IDXGIAdapter1** ppAdapter,
		bool requestHighPerformanceAdapter = true)
	{
		*ppAdapter = nullptr;

		ComPtr<IDXGIAdapter1> adapter;

		ComPtr<IDXGIFactory6> factory6;
		if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
		{
			for (
				UINT adapterIndex = 0;
				SUCCEEDED(factory6->EnumAdapterByGpuPreference(
					adapterIndex,
					requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
					IID_PPV_ARGS(&adapter)));
					++adapterIndex)
			{
				DXGI_ADAPTER_DESC1 desc;
				adapter->GetDesc1(&desc);

				if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				{
					// Don't select the Basic Render Driver adapter.
					// If you want a software adapter, pass in "/warp" on the command line.
					continue;
				}

				// Check to see whether the adapter supports Direct3D 12, but don't create the
				// actual device yet.
				if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
				{
					break;
				}
			}
		}

		if (adapter.Get() == nullptr)
		{
			for (UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex)
			{
				DXGI_ADAPTER_DESC1 desc;
				adapter->GetDesc1(&desc);

				if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				{
					// Don't select the Basic Render Driver adapter.
					// If you want a software adapter, pass in "/warp" on the command line.
					continue;
				}

				// Check to see whether the adapter supports Direct3D 12, but don't create the
				// actual device yet.
				if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
				{
					break;
				}
			}
		}

		if (adapter.Get() == nullptr)
		{
			//Fall back to software adapter
			for (UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex)
			{
				DXGI_ADAPTER_DESC1 desc;
				adapter->GetDesc1(&desc);
				if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				{
					if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
					{
						break;
					}
				}
			}
		}

		*ppAdapter = adapter.Detach();
	}

	RenderBackend_D3D12::RenderBackend_D3D12() :
		m_PendingInitializeObjects()
		, m_MemoryManager(this)
		, m_SamplerManager(this)
		, m_RootSignatureManager(this)
		, m_PipelineManager(this)
		, m_ComputePipelineManager(this)
		, m_DescriptorAllocatorSet(this)
		, p_IOManager(nullptr)
		, p_ResourceImporter(nullptr)
		, p_ResourceManager(nullptr)
		, m_GPUFrameManager(this)
	{
	}

	void RenderBackend_D3D12::Init(cacore::IModuleManager* pModuleManager)
	{
		catimer::SetGlobalTimerSystem(pModuleManager->GetInstance<catimer::TimerSystem>());
		m_ShaderResourceImporter.SetCompiler(pModuleManager->GetInstance<ShaderCompilerSlang::IShaderCompilerManager>());
		p_IOManager = pModuleManager->GetInstance<ca_io::IOManager>();
		p_ResourceManager = pModuleManager->GetInstance<resource_management::ResourceManagingSystem>();
		p_ResourceImporter = pModuleManager->GetInstance<resource_management::ResourceImportingSystem>();
		p_ResourceImporter->AddImporter(&m_ShaderResourceImporter);

		UINT dxgiFactoryFlags = 0;

#if D3D12_RENDER_BACKEND_DEBUG
		// Enable the debug layer (requires the Graphics Tools "optional feature").
		// NOTE: Enabling the debug layer after device creation will invalidate the active device.
		{
			ComPtr<ID3D12Debug> debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
			{
				debugController->EnableDebugLayer();

				// Enable additional debug layers.
				dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
			}
		}
#endif

		//ComPtr<IDXGIFactory4> factory;
		ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_Factory)));

		GetHardwareAdapter(m_Factory.Get(), &m_Adapter);

		ThrowIfFailed(D3D12CreateDevice(
			m_Adapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_Device)
		));

		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		ThrowIfFailed(m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue)));

		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		ThrowIfFailed(m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_ComputeQueue)));

		AfterDeviceInit();

		// editor-web-render-spike 2.1：把非绑定面的共享纹理适配器按实例名 "WebRenderSpike" 登记一次。
		// 适配器持有本后端指针（转发到同一设备/队列），测试用
		// `GetInstance<IWebRenderSpikeProvider>(CANAME("WebRenderSpike"))` 取到它。
		// **登记只发生在本 .cpp**：被 CodeGen 解析的 `RenderBackend_D3D12.h` 不含新接口的 include/继承。
		g_WebRenderSpikeModuleManager = pModuleManager;
		g_WebRenderSpikeProvider = std::make_unique<D3D12WebRenderSpikeProvider>(this);
		if (pModuleManager != nullptr)
		{
			pModuleManager->AddInstance<IWebRenderSpikeProvider>(CANAME("WebRenderSpike"),
				g_WebRenderSpikeProvider.get());
		}

	}

//	void RenderBackend_D3D12::Initialize(catimer::TimerSystem* timer
//		, ca_io::IOManager* ioManager
//		, resource_management::ResourceManagingSystem* resourceManager
//		, resource_management::ResourceImportingSystem* resourceImporter
//		, castl::shared_ptr <ShaderCompilerSlang::IShaderCompilerManager> shaderCompiler
//		, castl::string const& appName
//		, castl::string const& engineName)
//	{
//		catimer::SetGlobalTimerSystem(timer);
//		m_ShaderResourceImporter.SetCompiler(shaderCompiler.get());
//		p_IOManager = ioManager;
//		p_ResourceManager = resourceManager;
//		p_ResourceImporter = resourceImporter;
//		p_ResourceImporter->AddImporter(&m_ShaderResourceImporter);
//
//		UINT dxgiFactoryFlags = 0;
//
//#if D3D12_RENDER_BACKEND_DEBUG
//		// Enable the debug layer (requires the Graphics Tools "optional feature").
//		// NOTE: Enabling the debug layer after device creation will invalidate the active device.
//		{
//			ComPtr<ID3D12Debug> debugController;
//			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
//			{
//				debugController->EnableDebugLayer();
//
//				// Enable additional debug layers.
//				dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
//			}
//		}
//#endif
//
//		//ComPtr<IDXGIFactory4> factory;
//		ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_Factory)));
//
//		GetHardwareAdapter(m_Factory.Get(), &m_Adapter);
//
//		ThrowIfFailed(D3D12CreateDevice(
//			m_Adapter.Get(),
//			D3D_FEATURE_LEVEL_11_0,
//			IID_PPV_ARGS(&m_Device)
//		));
//
//		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
//		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
//		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
//		ThrowIfFailed(m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue)));
//
//		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
//		ThrowIfFailed(m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_ComputeQueue)));
//
//		AfterDeviceInit();
//	}

	void RenderBackend_D3D12::Release()
	{
		// editor-web-render-spike 2.1：先注销/销毁非绑定面适配器 —— 它持有的纹理助手持设备/队列的 COM
		// 引用，必须早于下面把 m_Device / m_CommandQueue 置空；同时避免模块管理器的注册表里留悬空指针。
		if (g_WebRenderSpikeModuleManager != nullptr)
		{
			g_WebRenderSpikeModuleManager->RemoveInstance<IWebRenderSpikeProvider>(CANAME("WebRenderSpike"));
			g_WebRenderSpikeModuleManager = nullptr;
		}
		g_WebRenderSpikeProvider.reset();
		// D7: flush both queues (signal + wait) BEFORE releasing frame contexts and
		// swapchains. The last Present is queued AFTER the frame's Signal
		// (GPUGraphExecutor.cpp:2241→2245), so FrameContext::GPUWaitIdle (which waits the
		// frame fence) does NOT cover the present — destroying a swapchain while its present
		// is still in flight makes the D3D12 debug layer report that a resource was destroyed
		// while GPU work still references it. WaitIdle
		// signals a fresh value after the present and waits for it.
		WaitIdle();
		m_GPUFrameManager.Release();

		// D7: release window contexts (swapchain + backbuffers) while the device and the
		// descriptor-allocator heaps are still alive — safe now that the queues (incl. the
		// last present) are flushed. WindowContext's Release() only releases its frame
		// semaphore; the swapchain and back buffers are freed by ~WindowContext, so clearing
		// the map destroys them.
		m_WindowContexts.clear();

		m_SamplerManager.Release();
		m_RootSignatureManager.Release();
		m_PipelineManager.Release();
		m_ComputePipelineManager.Release();
		m_DescriptorAllocatorSet.Release();
		m_MemoryManager.Release();

		m_CommandQueue = nullptr;
		m_ComputeQueue = nullptr;
		m_Device = nullptr;
		m_Adapter = nullptr;
		m_Factory = nullptr;
	}

	void RenderBackend_D3D12::WaitIdle()
	{
		// Real implementation: flush both queues (direct + compute) by signaling a fresh
		// fence value and waiting for it. The fresh value is signaled AFTER the last Present
		// is queued (GPUGraphExecutor.cpp:2241→2245), so waiting covers the render work that
		// precedes the present — on return the GPU is idle, and local D3D12 resources may be
		// destroyed without the debug layer complaining that GPU work still references them.
		m_GPUFrameManager.WaitIdle();
	}

	RenderBackend_D3D12::~RenderBackend_D3D12()
	{
		Release();
	}

	castl::shared_ptr<WindowHandle> RenderBackend_D3D12::GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window)
	{
		auto found = m_WindowContexts.find(window);
		if (found != m_WindowContexts.end())
		{
			return found->second;
		}
		castl::shared_ptr<WindowContext> newHandle = castl::make_shared<WindowContext>(this, window);
		m_WindowContexts.insert(castl::make_pair(window, newHandle));
		return newHandle;
	}

	bool RenderBackend_D3D12::AnyWindowRunning()
	{
		return !m_WindowContexts.empty();
	}

	void RenderBackend_D3D12::CleanupWindowHandles()
	{
		bool needWait = false;
		for(auto& pair : m_WindowContexts)
		{
			if(pair.first->WindowShouldClose() || pair.second->NeedResize())
			{
				needWait = true;
				break;
			}
		}
		if (!needWait)
		{
			return;
		}

		m_GPUFrameManager.WaitIdle();
		auto itr = m_WindowContexts.begin();
		while(itr != m_WindowContexts.end())
		{
			if (itr->first->WindowShouldClose())
			{
				itr->second->Release();
				itr = m_WindowContexts.erase(itr);
			}
			else
			{
				itr->second->CheckResize();
				++itr;
			}
		}
	}

	//void RenderBackend_D3D12::ScheduleGPUFrame(TaskScheduler* scheduler, GPUFrame const& gpuFrame)
	//{
	//	GPUFrameManager::PFrameContext frameContext = m_GPUFrameManager.AquireFrameContext();
	//	auto pGraph = gpuFrame.pGraph;
	//	D3D12GPUGraphExecutor executor(this);
	//	executor.CompileAndExecute(*pGraph.get(), std::move(frameContext));
	//	CleanupWindowHandles();
	//}

	void RenderBackend_D3D12::ExecuteGraph(TaskScheduler* scheduler
		, castl::shared_ptr<GPUGraph> const& graph)
	{
		CleanupWindowHandles();

		GPUFrameManager::PFrameContext frameContext = m_GPUFrameManager.AquireFrameContext();
		D3D12GPUGraphExecutor executor(this);
		executor.CompileAndExecute(*graph.get(), std::move(frameContext));
		executor.Release();
	}

	// vulkan-gpu-queries（能力 `gpu-graph-timing`）任务 2.1：D3D12 的逐 pass GPU 计时。
	// 能力按队列查询：队列存在且能报出非零时间戳频率即视为支持（design D4/D7）。
	GPUTimingCapabilities RenderBackend_D3D12::GetTimingCapabilities(EGPUQueueType queue) const
	{
		if (!m_Device)
		{
			return GPUTimingCapabilities{};
		}
		ComPtr<ID3D12CommandQueue> pQueue;
		if (queue == EGPUQueueType::eDirect)
		{
			pQueue = m_CommandQueue;
		}
		else if (queue == EGPUQueueType::eCompute)
		{
			pQueue = m_ComputeQueue;
		}
		if (!pQueue)
		{
			return GPUTimingCapabilities{};
		}
		UINT64 frequency = 0;
		if (FAILED(pQueue->GetTimestampFrequency(&frequency)) || frequency == 0)
		{
			return GPUTimingCapabilities{};
		}
		GPUTimingCapabilities result{};
		result.passTimestamps = true;
		return result;
	}

	std::unique_ptr<IGPUGraphTiming> RenderBackend_D3D12::ExecuteGraphTimed(
		TaskScheduler* scheduler
		, castl::shared_ptr<GPUGraph> const& graph)
	{
		(void)scheduler;
		if (!graph)
		{
			CA_LOG_ERR("RenderBackend_D3D12::ExecuteGraphTimed: 图为空，无法计时");
			return nullptr;
		}

		// 结果对象自己持有查询堆 / 回读缓冲 / fence（design D5），执行器返回即析构也不影响它。
		auto pTiming = D3D12GPUTiming::Create(this, *graph.get(), ++m_TimingSubmissionCounter);
		if (!pTiming)
		{
			// 具体原因（哪个队列不支持 / 哪次分配失败）已由 D3D12GPUTiming::Create 记录。
			return nullptr;
		}

		CleanupWindowHandles();

		GPUFrameManager::PFrameContext frameContext = m_GPUFrameManager.AquireFrameContext();
		D3D12GPUGraphExecutor executor(this);
		executor.SetTimingRecorder(pTiming.get());
		executor.CompileAndExecute(*graph.get(), std::move(frameContext));
		executor.Release();

		// 提交之后在两条队列上给本对象自己的 fence 发信号，再等待并读取（design D7）。
		pTiming->SignalAfterExecution(this);
		if (!pTiming->Finish(this))
		{
			return nullptr;
		}
		return pTiming;
	}

	castl::shared_ptr<GPUBuffer> RenderBackend_D3D12::CreateGPUBuffer(GPUBufferDescriptor const& descriptor, EBufferUsageFlags usageFlags)
	{
		castl::shared_ptr<D3DBufferObject> result = castl::make_shared<D3DBufferObject>(this);
		D3D12_RESOURCE_DESC resourceDesc = GetResourceDescFromGPUBufferDescriptor(descriptor, usageFlags);
		GPUResource resource = m_MemoryManager.AllocGPUResource(resourceDesc, D3D12_HEAP_TYPE_DEFAULT);
		result->SetDescriptor(descriptor);
		result->SetGPUResource(castl::move(resource));
		return result;
	}



	castl::shared_ptr<GPUTexture> RenderBackend_D3D12::CreateGPUTexture(GPUTextureDescriptor const& inDescriptor, ETextureAccessTypeFlags accessType)
	{
		castl::shared_ptr<D3DImageObject> result = castl::make_shared<D3DImageObject>(this);
		D3D12_RESOURCE_DESC resourceDesc = GetResourceDescFromTextureDescriptor(inDescriptor, accessType);
		GPUResource resource = m_MemoryManager.AllocGPUResource(resourceDesc, D3D12_HEAP_TYPE_DEFAULT);
		result->SetDescriptor(inDescriptor);
		result->SetGPUResource(castl::move(resource));
		return result;
	}

	castl::shared_ptr<ShaderStruct> RenderBackend_D3D12::CreateShaderStruct(cacore::NameHash const& structType)
	{
		auto shaderLibrary = p_ResourceManager->GetOrLoadResource<ShaderLibrary>("D3D12ShaderLibrary.shLib");
		auto found = shaderLibrary->m_ShaderStructs.find(structType);
		ShaderCompilerSlang::ShaderStructData const* pData = nullptr;
		if (found != shaderLibrary->m_ShaderStructs.end())
		{
			pData = &found->second;
		}
		return NewSubObject_Shared<D3D2ShaderStruct>(pData);
	}

	// csharp-render-shader-constants 任务 1.1：未命中返回空指针且不打断。直接读公开段的两张表，
	// 与上面的 `CreateShaderStruct` 用同一条路径（同一个资源名、同一张表）。
	ShaderCompilerSlang::ShaderStructData const* RenderBackend_D3D12::FindShaderStructData(
		cacore::NameHash const& structType)
	{
		auto shaderLibrary = p_ResourceManager->GetOrLoadResource<ShaderLibrary>("D3D12ShaderLibrary.shLib");
		if (shaderLibrary == nullptr)
		{
			return nullptr;
		}
		auto found = shaderLibrary->m_ShaderStructs.find(structType);
		return found == shaderLibrary->m_ShaderStructs.end() ? nullptr : &found->second;
	}

	ShaderCompilerSlang::ShaderStructData const* RenderBackend_D3D12::FindShaderRootStructData(
		cacore::PathHash const& shaderPath)
	{
		auto shaderLibrary = p_ResourceManager->GetOrLoadResource<ShaderLibrary>("D3D12ShaderLibrary.shLib");
		if (shaderLibrary == nullptr)
		{
			return nullptr;
		}
		auto found = shaderLibrary->m_ShaderRootStructs.find(shaderPath);
		return found == shaderLibrary->m_ShaderRootStructs.end() ? nullptr : &found->second;
	}

	void RenderBackend_D3D12::RunTestCode()
	{
		//struct VertexStruct
		//{
		//	std::array<float, 3> pos;
		//	std::array<float, 3> color;
		//};
		//cacore::HashObj<VertexInputsDescriptor> descs = VertexInputsDescriptor::Create(sizeof(VertexStruct),
		//	{
		//		VertexAttribute::Create(offsetof(VertexStruct, pos), VertexInputFormat::eR32G32B32_SFloat, CANAME("POSITION")),
		//		VertexAttribute::Create(offsetof(VertexStruct, color), VertexInputFormat::eR32G32B32_SFloat, CANAME("COLOR")),
		//	}, false);

		//std::vector<VertexStruct> testBuffer = {
		//	{{-0.25f, -0.25f, -0.25f }, {1.0f, 0.0f, 0.0f}},
		//	{{0.25f, -0.25f, -0.25f }, {0.0f, 1.0f, 0.0f}},
		//	{{0.0f, 0.5f, 0.0f }, {0.0f, 0.0f, 1.0f}},
		//};

		//ImageHandle image(CANAME("TestImage"));
		//BufferHandle vbuffer(CANAME("TestVertBuffer"));
		//GPUGraph newGraph;
		//newGraph.AllocImage(image, GPUTextureDescriptor::Create(1024, 720, ETextureFormat::E_R8G8B8A8_UNORM, ETextureAccessType::eRT));
		//newGraph.AllocBuffer(vbuffer, GPUBufferDescriptor::Create(EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, testBuffer.size(), sizeof(testBuffer[0])));
		//newGraph.ScheduleData(vbuffer, testBuffer.data(), testBuffer.size() * sizeof(testBuffer[0]));
		//newGraph.AddPass(
		//	RenderPass::New({ image })
		//	.SetShaderInfo({ CAPATH("Shaders/Test/TestSimpleTriangle") })
		//	.DrawCall
		//	(
		//		DrawCallBatch::New()
		//		.VertexStream(CANAME("TestVerticesInput"), descs)
		//		.DrawCall(
		//			DrawCall::New()
		//			.SetVertexBuffer(CANAME("TestVerticesInput"), vbuffer)
		//			.Draw(testBuffer.size())
		//		)
		//	)
		//);

		//D3D12GPUGraphExecutor executor(this);
		//executor.CompileAndExecute(newGraph);
	}


	ShaderFileInfo const* RenderBackend_D3D12::GetShaderFileInfo(ShaderInfo const& shaderInfo)
	{
		auto shaderLibrary = p_ResourceManager->GetOrLoadResource<ShaderLibrary>("D3D12ShaderLibrary.shLib");
		auto fileInfo = shaderLibrary->GetShaderFileInfo(shaderInfo.path);
		return fileInfo;
	}

	ShaderSetData RenderBackend_D3D12::GetShaderCodes(ShaderInfo const& shaderInfo)
	{
		ShaderSetData result;
		auto shaderLibrary = p_ResourceManager->GetOrLoadResource<ShaderLibrary>("D3D12ShaderLibrary.shLib");
		auto fileInfo = shaderLibrary->GetShaderFileInfo(shaderInfo.path);
		result.reflectionData = &fileInfo->reflectionData;
		for (auto& fileInfo : fileInfo->entryPointToShaderProgram)
		{
			auto code = shaderLibrary->GetShaderCode(fileInfo.programHash);

			switch (code->shaderType)
			{
			case ECompileShaderType::eVert:
				result.vertexShader = code->data;
				break;
			case ECompileShaderType::eFrag:
				result.fragmentShader = code->data;
				break;
			case ECompileShaderType::eComp:
				result.computeShader = code->data;
				break;
			default:
				break;
			}
			result;
		}
		return result;
	}

	void RenderBackend_D3D12::OnDeviceInit(castl::function<void()>const& callback)
	{
		if (DeviceInited())
		{
			callback();
			return;
		}
		if (!m_PendingDeviceInitCallbacks.push_back_if(callback, [&]()->bool
		{
			return !DeviceInited();
		}))
		{
			CA_ASSERT_BREAK(DeviceInited(), "Device Should Init Here Now!");
			callback();
			return;
		}
	}

	void RenderBackend_D3D12::AfterDeviceInit()
	{
		m_PendingDeviceInitCallbacks.clear([&](auto& callback)
		{
			callback();
		});
	}

	// --- add-render-readback: D3D12 GPU→host readback ---

	// Block host until the GPU has passed `value` on `fence` (used by the synchronous readback).
	static void WaitD3D12Fence(ID3D12Fence* fence, uint64_t value)
	{
		if (fence == nullptr) return;
		HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		fence->SetEventOnCompletion(value, eventHandle);
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	// D3D12 readback token. The D3D12 path is SYNCHRONOUS (record → execute → signal → wait →
	// copy into dst all happen inside Readback(), which releases its COM objects before returning),
	// so the token holds only the copy metadata and is already filled → Wait() is a no-op. (The
	// earlier per-call COM-lifetime design dangled a COM object in the async token; sync removes it.)
	class D3D12ReadbackToken : public IReadbackToken
	{
	public:
		D3D12ReadbackToken(RenderBackend_D3D12* backend, std::span<uint8_t> dst)
			: m_Backend(backend), m_Dst(dst) {}
		~D3D12ReadbackToken() override { Release(); }

		void Wait() override
		{
			if (m_Filled) return;
			WaitFence();
			UINT8* pData = nullptr;
			if (m_ReadbackResource && SUCCEEDED(m_ReadbackResource->Map(0, nullptr, reinterpret_cast<void**>(&pData)) && pData))
			{
				if (m_IsImage)
				{
					uint64_t srcStride = m_SrcRowPitch;            // padded (footprint) row
					uint64_t dstStride = (uint64_t)m_Width * m_Bpp; // compact row
					for (uint32_t r = 0; r < m_Height; ++r)
					{
						memcpy(m_Dst.data() + r * dstStride, pData + r * srcStride, static_cast<size_t>(dstStride));
					}
				}
				else
				{
					size_t n = static_cast<size_t>(m_ByteSize);
					if (n > m_Dst.size()) n = m_Dst.size();
					memcpy(m_Dst.data(), pData, n);
				}
				m_ReadbackResource->Unmap(0, nullptr);
			}
			m_Filled = true;
		}

		bool IsReady() const override { return m_Filled; }
		void Reset() override { Release(); }

		// Synchronous readback + host-visible-buffer direct path: data already in dst, no COM
		// staging to own. Wait() is a no-op (m_Filled=true).
		void SetCompleted(uint64_t byteSize, bool isImage, uint32_t width, uint32_t height, uint32_t rowPitch)
		{
			m_ByteSize = byteSize; m_IsImage = isImage;
			m_Width = width; m_Height = height; m_RowPitch = rowPitch; m_Filled = true;
		}
		void SetStaging(D3D12MA::Allocation* alloc, ID3D12Resource* resource,
			ComPtr<ID3D12CommandAllocator>&& allocator, ComPtr<ID3D12GraphicsCommandList7>&& cmdList,
			ComPtr<ID3D12Fence>&& fence, uint64_t fenceValue,
			uint64_t byteSize, bool isImage, uint32_t width, uint32_t height, uint32_t bpp,
			uint32_t rowPitch, uint64_t srcRowPitch)
		{
			m_ReadbackAlloc = alloc;
			m_ReadbackResource = resource;
			m_Allocator = std::move(allocator);
			m_CommandList = std::move(cmdList);
			m_Fence = std::move(fence);
			m_FenceValue = fenceValue;
			m_ByteSize = byteSize;
			m_IsImage = isImage;
			m_Width = width; m_Height = height; m_Bpp = bpp; m_RowPitch = rowPitch; m_SrcRowPitch = srcRowPitch;
		}

	private:
		void WaitFence()
		{
			if (m_Fence)
			{
				HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
				m_Fence->SetEventOnCompletion(m_FenceValue, eventHandle);
				WaitForSingleObject(eventHandle, INFINITE);
				CloseHandle(eventHandle);
			}
		}

		void Release()
		{
			WaitFence(); // ensure GPU is done with the READBACK buffer before releasing it (no UAF, no in-flight free)
			// Release the COMMAND LIST before its ALLOCATOR (a command list holds a reference to its
			// allocator; destroying the allocator first trips the D3D12 debug layer's live-reference check).
			if (m_CommandList) { m_CommandList->Release(); m_CommandList = nullptr; }
			if (m_Allocator) { m_Allocator->Release(); m_Allocator = nullptr; }
			if (m_Fence) { m_Fence->Release(); m_Fence = nullptr; }
			if (m_ReadbackAlloc) { m_ReadbackAlloc->Release(); m_ReadbackAlloc = nullptr; }
			m_ReadbackResource = nullptr;
		}

		RenderBackend_D3D12* m_Backend;
		std::span<uint8_t> m_Dst;
		ComPtr<ID3D12CommandAllocator> m_Allocator;
		ComPtr<ID3D12GraphicsCommandList7> m_CommandList;
		D3D12MA::Allocation* m_ReadbackAlloc = nullptr; // READBACK buffer's allocation (released in Reset/dtor)
		ID3D12Resource* m_ReadbackResource = nullptr;
		ComPtr<ID3D12Fence> m_Fence;
		uint64_t m_FenceValue = 0;
		uint64_t m_ByteSize = 0;
		bool m_IsImage = false;
		uint32_t m_Width = 0, m_Height = 0, m_Bpp = 0;
		uint64_t m_RowPitch = 0;   // compact row pitch (bytes per row in dst; returned to caller)
		uint64_t m_SrcRowPitch = 0;// padded READBACK footprint row pitch (source copy stride)
		bool m_Filled = false;
	};

	// --- editor-window-panel（⑩ 展示证明）：D3D12 窗口 backbuffer 呈现前捕获 ---
	//
	// 语义与 Vulkan 对齐：`Readback(Backbuffer)` 只登记请求并返回 token；真正的拷贝在
	// `PresentWindows` 调 `CaptureRequestedBackbuffer`（`IDXGISwapChain::Present` 之前）时完成，
	// dst 是格式原生字节序（D3D12 常见 R8G8B8A8 → RGBA）。
	class D3D12BackbufferReadbackToken : public IReadbackToken
	{
	public:
		D3D12BackbufferReadbackToken(RenderBackend_D3D12* backend, castl::shared_ptr<WindowHandle> window,
			std::shared_ptr<D3D12BackbufferReadbackState> state)
			: m_Backend(backend), m_Window(std::move(window)), m_State(std::move(state))
		{
		}

		~D3D12BackbufferReadbackToken() override { Reset(); }

		void Wait() override
		{
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!m_State->Ready.load())
			{
				if (m_State->Failed.load())
				{
					CA_LOG_ERR("D3D12BackbufferReadbackToken: {}", m_State->Error);
					return;
				}
				if (std::chrono::steady_clock::now() >= deadline)
				{
					CA_LOG_ERR("D3D12BackbufferReadbackToken: 等待 Present 前捕获超时（需要跑一帧含 Present 的图）");
					return;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}

		bool IsReady() const override { return m_State->Ready.load(); }

		void Reset() override
		{
			if (m_State && !m_State->Ready.load() && m_Backend != nullptr)
			{
				m_Backend->CancelBackbufferReadback(m_State.get());
			}
		}

	private:
		RenderBackend_D3D12* m_Backend = nullptr;
		castl::shared_ptr<WindowHandle> m_Window; // 保活：捕获发生在后续帧的 Present 上
		std::shared_ptr<D3D12BackbufferReadbackState> m_State;
	};

	std::unique_ptr<IReadbackToken> RenderBackend_D3D12::BeginBackbufferReadback(ImageHandle const& image,
		std::span<uint8_t> dst)
	{
		WindowContext* window = image.GetWindowPtr<WindowContext>();
		if (window == nullptr)
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(backbuffer) 窗口无效或已释放");
			return nullptr;
		}
		// 红态构造（工件修订认可）：真机交换链都支持 COPY_SOURCE，用同一自检开关强制走拒绝分支。
		if (std::getenv("CASCADED_READBACK_FORCE_NO_BB_TRANSFER") != nullptr)
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(backbuffer) 被拒绝：自检开关 "
			           "CASCADED_READBACK_FORCE_NO_BB_TRANSFER 强制报告交换链不支持 COPY_SOURCE");
			return nullptr;
		}
		auto const& desc = window->GetBackbufferDescriptor();
		uint64_t byteSize = GetImageByteSize(desc.width, desc.height, desc.format);
		if (byteSize == 0)
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(backbuffer) 格式 {} 不支持紧凑回读", (int)desc.format);
			return nullptr;
		}
		if (dst.size() < byteSize)
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(backbuffer) dst too small: {} < {} bytes", dst.size(), byteSize);
			return nullptr;
		}
		auto state = std::make_shared<D3D12BackbufferReadbackState>();
		state->Dst = dst;
		{
			std::lock_guard<std::mutex> lock(m_BackbufferReadbackMutex);
			if (m_BackbufferReadbacks.find(window) != m_BackbufferReadbacks.end())
			{
				CA_LOG_ERR("RenderBackend_D3D12: Readback(backbuffer) 该窗口已有未完成的捕获请求");
				return nullptr;
			}
			m_BackbufferReadbacks[window] = state;
		}
		CA_LOG_INFO("RenderBackend_D3D12: 已登记 backbuffer 呈现前捕获（{}x{}，{} 字节）", desc.width, desc.height,
		            byteSize);
		return std::make_unique<D3D12BackbufferReadbackToken>(this, image.GetWindowHandle(), state);
	}

	void RenderBackend_D3D12::CancelBackbufferReadback(D3D12BackbufferReadbackState* state)
	{
		if (state == nullptr)
		{
			return;
		}
		std::lock_guard<std::mutex> lock(m_BackbufferReadbackMutex);
		for (auto it = m_BackbufferReadbacks.begin(); it != m_BackbufferReadbacks.end(); ++it)
		{
			if (it->second.get() == state)
			{
				m_BackbufferReadbacks.erase(it);
				return;
			}
		}
	}

	void RenderBackend_D3D12::CaptureRequestedBackbuffer(WindowContext* window)
	{
		std::shared_ptr<D3D12BackbufferReadbackState> state;
		{
			std::lock_guard<std::mutex> lock(m_BackbufferReadbackMutex);
			auto it = m_BackbufferReadbacks.find(window);
			if (it == m_BackbufferReadbacks.end())
			{
				return;
			}
			state = it->second;
			m_BackbufferReadbacks.erase(it);
		}
		auto fail = [&state](std::string const& reason)
		{
			state->Error = reason;
			state->Failed = true;
			CA_LOG_ERR("RenderBackend_D3D12: backbuffer 呈现前捕获失败：{}", reason);
		};
		if (window == nullptr)
		{
			fail("窗口无效");
			return;
		}
		ID3D12Resource* srcResource = window->GetCurrentBackBufferResource().Get();
		if (srcResource == nullptr)
		{
			fail("当前 backbuffer 资源为空");
			return;
		}
		auto const& desc = window->GetBackbufferDescriptor();
		uint64_t byteSize = GetImageByteSize(desc.width, desc.height, desc.format);
		if (byteSize == 0 || state->Dst.size() < byteSize)
		{
			fail("格式或目标字节数不支持");
			return;
		}
		uint32_t bpp = GetFormatBlockSize(desc.format);
		uint64_t compactRowPitch = (uint64_t)desc.width * bpp;
		D3D12_RESOURCE_DESC srcDesc = srcResource->GetDesc();
		UINT64 totalBytes = 0, rowSize = 0;
		UINT numRows = 0;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
		m_Device->GetCopyableFootprints(&srcDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &totalBytes);
		GPUResource readback = m_MemoryManager.AllocReadbackStagingBuffer(totalBytes > 0 ? totalBytes : byteSize);
		D3D12MA::Allocation* readbackAlloc = readback.GetAllocation();
		ID3D12Resource* readbackResource = readback.GetResource();
		if (readbackAlloc == nullptr || readbackResource == nullptr)
		{
			fail("READBACK 缓冲区分配失败");
			return;
		}
		ComPtr<ID3D12CommandAllocator> allocator;
		ComPtr<ID3D12GraphicsCommandList7> cmdList;
		ComPtr<ID3D12Fence> fence;
		if (FAILED(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))
			|| FAILED(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
				IID_PPV_ARGS(&cmdList)))
			|| FAILED(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
		{
			readbackAlloc->Release();
			fail("命令分配器/命令列表/围栏创建失败");
			return;
		}
		uint64_t fenceValue = 1;
		ResourceState const& srcState = window->GetCurrentBackBufferResourceState();
		ResourceBarrierUsageStates srcUsages = DetermineResourceBarrierUsageStates(srcState);
		D3D12_TEXTURE_BARRIER toCopy{};
		toCopy.AccessBefore = srcUsages.accessState;
		toCopy.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
		toCopy.SyncBefore = srcUsages.barrierSync;
		toCopy.SyncAfter = D3D12_BARRIER_SYNC_COPY;
		toCopy.LayoutBefore = srcUsages.layoutState;
		toCopy.LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
		toCopy.pResource = srcResource;
		toCopy.Subresources = CD3DX12_BARRIER_SUBRESOURCE_RANGE(UINT_MAX);
		D3D12_TEXTURE_BARRIER rest{};
		rest.AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE;
		rest.AccessAfter = srcUsages.accessState;
		rest.SyncBefore = D3D12_BARRIER_SYNC_COPY;
		rest.SyncAfter = srcUsages.barrierSync;
		rest.LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
		rest.LayoutAfter = srcUsages.layoutState;
		rest.pResource = srcResource;
		rest.Subresources = CD3DX12_BARRIER_SUBRESOURCE_RANGE(UINT_MAX);
		CD3DX12_BARRIER_GROUP g1(1, &toCopy);
		cmdList->Barrier(1, &g1);
		D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
		dstLoc.pResource = readbackResource;
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		dstLoc.PlacedFootprint = footprint;
		dstLoc.PlacedFootprint.Offset = 0;
		D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
		srcLoc.pResource = srcResource;
		srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		srcLoc.SubresourceIndex = 0;
		cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
		CD3DX12_BARRIER_GROUP g2(1, &rest);
		cmdList->Barrier(1, &g2);
		cmdList->Close();
		ID3D12CommandList* cmdListRaw = cmdList.Get();
		m_CommandQueue->ExecuteCommandLists(1, &cmdListRaw);
		m_CommandQueue->Signal(fence.Get(), fenceValue);
		WaitD3D12Fence(fence.Get(), fenceValue);
		UINT8* pData = nullptr;
		if (SUCCEEDED(readbackResource->Map(0, nullptr, reinterpret_cast<void**>(&pData))) && pData)
		{
			uint64_t srcStride = footprint.Footprint.RowPitch;
			for (uint32_t r = 0; r < desc.height; ++r)
			{
				memcpy(state->Dst.data() + r * compactRowPitch, pData + r * srcStride,
				       static_cast<size_t>(compactRowPitch));
			}
			readbackResource->Unmap(0, nullptr);
		}
		cmdList.Reset();
		allocator.Reset();
		fence.Reset();
		readbackAlloc->Release();
		window->ApplyCurrentBackBufferResourceState(srcState);
		state->Ready = true;
		CA_LOG_INFO("RenderBackend_D3D12: backbuffer 呈现前捕获完成（{}x{}，{} 字节）", desc.width, desc.height,
		            byteSize);
	}

	std::unique_ptr<IReadbackToken> RenderBackend_D3D12::Readback(ImageHandle const& image, std::span<uint8_t> dst)
	{
		// editor-window-panel（⑩ 展示证明）：窗口 backbuffer → 登记"Present 前捕获"，由 PresentWindows 执行。
		if (image.GetType() == ImageHandle::ImageType::Backbuffer)
		{
			return BeginBackbufferReadback(image, dst);
		}
		if (image.GetType() != ImageHandle::ImageType::External)
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(image) only supports External resources (got type {}); "
				"graph-internal/backbuffer readback deferred", (int)image.GetType());
			return nullptr;
		}
		D3DImageObject* img = image.GetTexturePtr<D3DImageObject>();
		if (img == nullptr)
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(image) null D3DImageObject");
			return nullptr;
		}
		auto const& desc = img->GetDescriptor();
		uint32_t width = desc.width, height = desc.height;
		// Carrier byte size = the single public source (Common.h). Non-zero means "size computable",
		// NOT "readback accepts it" — acceptability is the support set below.
		uint64_t byteSize = GetImageByteSize(width, height, desc.format);
		if (byteSize == 0)
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(image) format {} unsupported (no compact layout / compressed / unknown)",
				(int)desc.format);
			return nullptr;
		}
		if (IsDepthStencilFormat(desc.format) && !IsDepthOnlyFormat(desc.format))
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(image) depth-stencil multi-aspect format {} "
				"unsupported (needs aspect/plane selection + row alignment; single-depth accepted)", (int)desc.format);
			return nullptr;
		}
		// Compact block size (internal intermediate) — drives the compact row stride below.
		uint32_t bpp = GetFormatBlockSize(desc.format);
		uint32_t rowPitch = width * bpp;
		if (dst.size() < byteSize)
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(image) dst too small: {} < {} bytes", dst.size(), byteSize);
			return nullptr;
		}
		// Subresource narrowing: the copy is hardcoded to subresource 0; anything else would be silently
		// truncated. Only single-mip / single-layer / non-MSAA / 2D is supported.
		// desc.samples is EMultiSampleCount (enum class, e1=0) — compare against e1, never ==1 />1.
		if (desc.mipLevels > 1 || desc.layers > 1
			|| desc.samples != EMultiSampleCount::e1 || desc.textureType != ETextureType::e2D)
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(image) only supports single-mip/single-layer/non-MSAA/2D "
				"(got mip={} layer={} samples={} type={})",
				desc.mipLevels, desc.layers, (int)desc.samples, (int)desc.textureType);
			return nullptr;
		}

		ID3D12Resource* srcResource = img->GetGPUResource().GetResource();
		D3D12_RESOURCE_DESC srcDesc = srcResource->GetDesc();

		// READBACK buffer of the footprint size (row pitch 256 / placement 512 aligned by the API).
		UINT64 totalBytes = 0, rowSize = 0; UINT numRows = 0;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
		m_Device->GetCopyableFootprints(&srcDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &totalBytes);
		GPUResource readback = m_MemoryManager.AllocReadbackStagingBuffer(totalBytes > 0 ? totalBytes : byteSize);
		D3D12MA::Allocation* readbackAlloc = readback.GetAllocation();
		ID3D12Resource* readbackResource = readback.GetResource();
		if (readbackAlloc == nullptr || readbackResource == nullptr)
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(image) readback buffer alloc failed");
			return nullptr;
		}

		// One-shot command allocator + graphics command list (infrequent capture) + signal fence.
		ComPtr<ID3D12CommandAllocator> allocator;
		ComPtr<ID3D12GraphicsCommandList7> cmdList;
		ComPtr<ID3D12Fence> fence;
		if (FAILED(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))
			|| FAILED(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmdList)))
			|| FAILED(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(image) cmd allocator/list/fence creation failed");
			if (readback.GetAllocation()) readback.Release();
			return nullptr;
		}
		uint64_t fenceValue = 1;

		// Source → COPY_SOURCE (enhanced barrier, matching the engine's barrier model).
		ResourceState const& srcState = img->GetResourceState();
		ResourceBarrierUsageStates srcUsages = DetermineResourceBarrierUsageStates(srcState);
		D3D12_TEXTURE_BARRIER toCopy{};
		toCopy.AccessBefore = srcUsages.accessState;
		toCopy.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
		toCopy.SyncBefore = srcUsages.barrierSync;
		toCopy.SyncAfter = D3D12_BARRIER_SYNC_COPY;
		toCopy.LayoutBefore = srcUsages.layoutState;
		toCopy.LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
		toCopy.pResource = srcResource;
		toCopy.Subresources = CD3DX12_BARRIER_SUBRESOURCE_RANGE(UINT_MAX);

		D3D12_TEXTURE_BARRIER rest{};
		rest.AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE;
		rest.AccessAfter = srcUsages.accessState;
		rest.SyncBefore = D3D12_BARRIER_SYNC_COPY;
		rest.SyncAfter = srcUsages.barrierSync;
		rest.LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
		rest.LayoutAfter = srcUsages.layoutState;
		rest.pResource = srcResource;
		rest.Subresources = CD3DX12_BARRIER_SUBRESOURCE_RANGE(UINT_MAX);

		CD3DX12_BARRIER_GROUP g1(1, &toCopy);
		cmdList->Barrier(1, &g1);

		D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
		dstLoc.pResource = readbackResource;
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		dstLoc.PlacedFootprint = footprint;
		dstLoc.PlacedFootprint.Offset = 0;
		D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
		srcLoc.pResource = srcResource;
		srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		srcLoc.SubresourceIndex = 0;
		cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

		CD3DX12_BARRIER_GROUP g2(1, &rest);
		cmdList->Barrier(1, &g2);
		cmdList->Close();

		ID3D12CommandList* cmdListRaw = cmdList.Get();
		m_CommandQueue->ExecuteCommandLists(1, &cmdListRaw);
		m_CommandQueue->Signal(fence.Get(), fenceValue);

		// Synchronous completion: wait the GPU copy, then copy compact bytes from the READBACK
		// buffer (row-by-row to drop the footprint's pitch padding) into dst.
		WaitD3D12Fence(fence.Get(), fenceValue);
		UINT8* pData = nullptr;
		if (SUCCEEDED(readbackResource->Map(0, nullptr, reinterpret_cast<void**>(&pData))) && pData)
		{
			uint64_t srcStride = footprint.Footprint.RowPitch;
			uint64_t dstStride = (uint64_t)width * bpp;
			for (uint32_t r = 0; r < height; ++r)
			{
				memcpy(dst.data() + r * dstStride, pData + r * srcStride, static_cast<size_t>(dstStride));
			}
			readbackResource->Unmap(0, nullptr);
		}
		// Drop command list before allocator (list holds an allocator ref), then free READBACK buffer.
		cmdList.Reset();
		allocator.Reset();
		fence.Reset();
		readbackAlloc->Release();

		auto token = std::make_unique<D3D12ReadbackToken>(this, dst);
		token->SetCompleted(byteSize, true, width, height, rowPitch);
		return token;
	}

	std::unique_ptr<IReadbackToken> RenderBackend_D3D12::Readback(BufferHandle const& buffer, std::span<uint8_t> dst)
	{
		if (buffer.GetType() != BufferHandle::BufferType::External)
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(buffer) only supports External resources (got type {}); "
				"graph-internal readback deferred", (int)buffer.GetType());
			return nullptr;
		}
		D3DBufferObject* buf = buffer.GetBufferPtr<D3DBufferObject>();
		if (buf == nullptr)
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(buffer) null D3DBufferObject");
			return nullptr;
		}
		uint64_t byteSize = buf->GetDescriptor().SizeInByte();
		if (dst.size() < byteSize)
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(buffer) dst too small: {} < {} bytes", dst.size(), byteSize);
			return nullptr;
		}

		auto token = std::make_unique<D3D12ReadbackToken>(this, dst);

		// CPU-readable heap (UPLOAD/CPU-visible buffer) → direct map, no GPU copy.
		ID3D12Resource* srcResource = buf->GetGPUResource().GetResource();
		{
			UINT8* pData = nullptr;
			if (srcResource && SUCCEEDED(srcResource->Map(0, nullptr, reinterpret_cast<void**>(&pData)) && pData))
			{
				size_t n = static_cast<size_t>(byteSize);
				if (n > dst.size()) n = dst.size();
				memcpy(dst.data(), pData, n);
				srcResource->Unmap(0, nullptr);
				token->SetCompleted(byteSize, false, 0, 0, 0);
				return token;
			}
		}
		// srcResource->Map may return null (or fail) for a non-host-visible DEFAULT buffer → fall to GPU copy.
		if (srcResource == nullptr)
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(buffer) null source resource");
			return nullptr;
		}

		GPUResource readback = m_MemoryManager.AllocReadbackStagingBuffer(byteSize);
		D3D12MA::Allocation* readbackAlloc = readback.GetAllocation();
		ID3D12Resource* readbackResource = readback.GetResource();
		if (readbackAlloc == nullptr || readbackResource == nullptr)
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(buffer) readback buffer alloc failed");
			return nullptr;
		}

		ComPtr<ID3D12CommandAllocator> allocator;
		ComPtr<ID3D12GraphicsCommandList7> cmdList;
		ComPtr<ID3D12Fence> fence;
		if (FAILED(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))
			|| FAILED(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmdList)))
			|| FAILED(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
		{
			CA_LOG_ERR("RenderBackend_D3D12: Readback(buffer) cmd allocator/list/fence creation failed");
			if (readback.GetAllocation()) readback.Release();
			return nullptr;
		}
		uint64_t fenceValue = 1;

		ResourceState const& srcState = buf->GetResourceState();
		ResourceBarrierUsageStates srcUsages = DetermineResourceBarrierUsageStates(srcState);
		D3D12_BUFFER_BARRIER toCopy{};
		toCopy.AccessBefore = srcUsages.accessState;
		toCopy.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
		toCopy.SyncBefore = srcUsages.barrierSync;
		toCopy.SyncAfter = D3D12_BARRIER_SYNC_COPY;
		toCopy.pResource = srcResource;
		toCopy.Offset = 0;
		toCopy.Size = ULLONG_MAX;
		D3D12_BUFFER_BARRIER rest{};
		rest.AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE;
		rest.AccessAfter = srcUsages.accessState;
		rest.SyncBefore = D3D12_BARRIER_SYNC_COPY;
		rest.SyncAfter = srcUsages.barrierSync;
		rest.pResource = srcResource;
		rest.Offset = 0;
		rest.Size = ULLONG_MAX;

		CD3DX12_BARRIER_GROUP g1(1, &toCopy);
		cmdList->Barrier(1, &g1);
		cmdList->CopyBufferRegion(srcResource, 0, readbackResource, 0, byteSize);
		CD3DX12_BARRIER_GROUP g2(1, &rest);
		cmdList->Barrier(1, &g2);
		cmdList->Close();

		ID3D12CommandList* cmdListRaw = cmdList.Get();
		m_CommandQueue->ExecuteCommandLists(1, &cmdListRaw);
		m_CommandQueue->Signal(fence.Get(), fenceValue);

		WaitD3D12Fence(fence.Get(), fenceValue);
		UINT8* pData = nullptr;
		if (SUCCEEDED(readbackResource->Map(0, nullptr, reinterpret_cast<void**>(&pData))) && pData)
		{
			size_t n = static_cast<size_t>(byteSize);
			if (n > dst.size()) n = dst.size();
			memcpy(dst.data(), pData, n);
			readbackResource->Unmap(0, nullptr);
		}
		cmdList.Reset();
		allocator.Reset();
		fence.Reset();
		readbackAlloc->Release();

		token->SetCompleted(byteSize, false, 0, 0, 0);
		return token;
	}

	//CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(CRenderBackend, RenderBackend_D3D12)
}

CA_MODULE_INSTANCE(graphics_backend::CRenderBackend, graphics_backend::RenderBackend_D3D12, RenderBackend_D3D12);