#include <cstdlib>
#define NOMINMAX
#include <windows.h>
#include <ThreadManager.h>
#include <Compiler.h>
#include <iostream>
#include <library_loader.h>
#include <CRenderBackend.h>
#include <RenderInterfaceManager.h>
#include <CNativeRenderPassInfo.h>
#include <CCommandList.h>
#include <ShaderBindingBuilder.h>
#include <FileLoader.h>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <thread>
#include <chrono>
#include <CASTL/CAString.h>
#include <CASTL/CAChrono.h>
#include <CAResource/ResourceSystemFactory.h>
#include <GPUGraph.h>
#include <TextureSampler.h>
#include <CAWindow/WindowSystem.h>
#include <CATimer/Timer.h>
#include <TimerSystemEditor/TimerSystem_Impl.h>
#include <IOManager/IOManager.h>
#include <filesystem>
#include <magic_enum/magic_enum.hpp>
#include <CAResource/ResourceSystemFactory.h>

using namespace thread_management;
using namespace resource_management;
using namespace library_loader;
using namespace graphics_backend;
using namespace cawindow;
using namespace catimer;
using namespace ca_io;


int main(int argc, char* argv[])
{
	TModuleLoader<ShaderCompilerSlang::IShaderCompilerManager> shaderManager("ShaderCompilerSlang");
	castl::shared_ptr < ShaderCompilerSlang::IShaderCompilerManager> shaderCompilerManager = shaderManager.New();
	shaderCompilerManager->InitializePoolSize(1);



	std::filesystem::path rootPathFS{ "../../../../" , std::filesystem::path::format::native_format };
	std::filesystem::path rootPath = std::filesystem::absolute(rootPathFS);
	castl::string resourceString = castl::to_ca(rootPath.string()) + "CAResources";
	castl::string assetString = castl::to_ca(rootPath.string()) + "CAAssets";
	castl::string editorResourceString = castl::to_ca(rootPath.string()) + "EditorConfigs";


	TModuleLoader<CThreadManager> threadManagerLoader("ThreadManager");
	TModuleLoader<CRenderBackend> renderBackendLoader("D3D12RenderBackend");
	TModuleLoader<IWindowSystem> windowSystemLoader("WindowSystem");
	TModuleLoader<IOManager> ioManagerLoader("IOManager_FS");
	TModuleLoader<ResourceFactory> resourceSystemLoader("CAGeneralReourceSystem");

	//Timer System
	InitTimerSystem();

	//Window System
	auto windowSystem = windowSystemLoader.New();

	//Initialize Thread Manager
	auto pThreadManager = threadManagerLoader.New();
	unsigned int n = std::thread::hardware_concurrency();
	n = (n == 0) ? 5 : (castl::min)(n, 16u);
	pThreadManager->InitializeThreadCount(GetGlobalTimerSystem(), n);

	//Initialize IO Manager
	auto g_IOManager = ioManagerLoader.New();
	g_IOManager->Initialize(pThreadManager.get());

	//Resource System
	auto resourceSystemFactory = resourceSystemLoader.New();
	auto pResourceManagingSystem = resourceSystemFactory->NewManagingSystemShared();

	pResourceManagingSystem->Initialize(g_IOManager);
	pResourceManagingSystem->SetResourceRootPath(assetString);

	auto importingSystem = resourceSystemFactory->NewImportingSystemShared();
	importingSystem->SetResourceManager(pResourceManagingSystem.get());

	auto pBackend = renderBackendLoader.New();
	pBackend->Initialize(GetGlobalTimerSystem()
		, g_IOManager.get(), pResourceManagingSystem.get(), importingSystem.get()
		, "Test D3D12 Backend", "CASCADED Engine");
	importingSystem->ScanSourceDirectory(resourceString);

	//pBackend->RunTestCode();


	GPUTextureDescriptor textureDesc = GPUTextureDescriptor::Create(1024, 512, ETextureFormat::E_B8G8R8A8_UNORM, ETextureAccessType::eSampled | ETextureAccessType::eTransferDst);
	castl::shared_ptr<GPUTexture> texture = pBackend->CreateGPUTexture(textureDesc);

	auto newWindow = windowSystem->NewWindow(1024, 512, "Window System Window");
	auto windowHandle = pBackend->GetWindowHandle(newWindow.lock());



	{
		struct VertexStruct
		{
			std::array<float, 3> pos;
			std::array<float, 3> color;
		};
		cacore::HashObj<VertexInputsDescriptor> descs = VertexInputsDescriptor::Create(sizeof(VertexStruct),
			{
				VertexAttribute::Create(offsetof(VertexStruct, pos), VertexInputFormat::eR32G32B32_SFloat, CANAME("POSITION")),
				VertexAttribute::Create(offsetof(VertexStruct, color), VertexInputFormat::eR32G32B32_SFloat, CANAME("COLOR")),
			}, false);

		std::vector<VertexStruct> testBuffer = {
			{{-0.25f, -0.25f, -0.25f }, {1.0f, 0.0f, 0.0f}},
			{{0.25f, -0.25f, -0.25f }, {0.0f, 1.0f, 0.0f}},
			{{0.0f, 0.5f, 0.0f }, {0.0f, 0.0f, 1.0f}},
		};

		ImageHandle windowBackBuffer(windowHandle);
		ImageHandle image(CANAME("TestImage"));
		BufferHandle vbuffer(CANAME("TestVertBuffer"));
		castl::shared_ptr<GPUGraph> newGraph = castl::make_shared<GPUGraph>();
		newGraph->Present(windowBackBuffer);
		newGraph->AllocImage(image, GPUTextureDescriptor::Create(1024, 720, ETextureFormat::E_R8G8B8A8_UNORM, ETextureAccessType::eRT))
			.AllocBuffer(vbuffer, GPUBufferDescriptor::Create(EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, testBuffer.size(), sizeof(testBuffer[0])))
			.ScheduleData(vbuffer, testBuffer.data(), testBuffer.size() * sizeof(testBuffer[0]))
			.AddPass(
			RenderPass::New({ windowBackBuffer })
			.SetShaderInfo({ CAPATH("Shaders/Test/TestSimpleTriangle") })
			.DrawCall
			(
				DrawCallBatch::New()
				.VertexStream(CANAME("TestVerticesInput"), descs)
				.DrawCall(
					DrawCall::New()
					.SetVertexBuffer(CANAME("TestVerticesInput"), vbuffer)
					.Draw(testBuffer.size())
				)
			)
		);
		GPUFrame newFrame;
		newFrame.pGraph = newGraph;
		//newFrame.presentWindows.push_back(windowHandle);
		auto scheduler = pThreadManager->NewScheduler();
		pBackend->ScheduleGPUFrame(scheduler.get(), newFrame);
	}


	castl::shared_ptr<ShaderStruct> pCameraData = pBackend->CreateShaderStruct(CANAME("CameraData"));
	pCameraData->SetValue(CANAME("viewProjMatrix"), glm::mat4(1.0f));

	pThreadManager.reset();
	pBackend->Release();
	pBackend.reset();

	//gCPUProfiler.Shutdown();
	return EXIT_SUCCESS;
}
