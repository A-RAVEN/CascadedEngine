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

castl::shared_ptr<CThreadManager> g_ThreadManager;
castl::shared_ptr<CRenderBackend> g_GPUBackend;
castl::shared_ptr<IWindowSystem> g_WindowSystem;

void TestGPUGraph0()
{
	auto newWindow = g_WindowSystem->NewWindow(1024, 512, "Hello Triangle");
	auto windowHandle = g_GPUBackend->GetWindowHandle(newWindow.lock());
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
	newGraph->Present(windowBackBuffer)
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
	auto scheduler = g_ThreadManager->NewScheduler();
	g_GPUBackend->ScheduleGPUFrame(scheduler.get(), newFrame);
}

void TestGPUGraph1()
{
	auto newWindow = g_WindowSystem->NewWindow(1024, 512, "Hello Triangle With Color");
	auto windowHandle = g_GPUBackend->GetWindowHandle(newWindow.lock());
	struct VertexStruct
	{
		std::array<float, 3> pos;
	};
	cacore::HashObj<VertexInputsDescriptor> descs = VertexInputsDescriptor::Create(sizeof(VertexStruct),
		{
			VertexAttribute::Create(offsetof(VertexStruct, pos), VertexInputFormat::eR32G32B32_SFloat, CANAME("POSITION")),
		}, false);

	std::vector<VertexStruct> testBuffer = {
		{{-0.25f, -0.25f, -0.25f }},
		{{0.25f, -0.25f, -0.25f }},
		{{0.0f, 0.5f, 0.0f }},
	};

	castl::shared_ptr<ShaderStruct> pConstantColor = g_GPUBackend->CreateShaderStruct(CANAME("ConstantColor"));
	pConstantColor->SetValue(CANAME("color"), glm::vec3(1.0f, 0.5f, 1.0f));

	ImageHandle windowBackBuffer(windowHandle);
	BufferHandle vbuffer(CANAME("TestVertBuffer"));
	castl::shared_ptr<GPUGraph> newGraph = castl::make_shared<GPUGraph>();
	newGraph->Present(windowBackBuffer)
		.AllocBuffer(vbuffer, GPUBufferDescriptor::Create(EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, testBuffer.size(), sizeof(testBuffer[0])))
		.ScheduleData(vbuffer, testBuffer.data(), testBuffer.size() * sizeof(testBuffer[0]))
		.AddPass(
			RenderPass::New({ windowBackBuffer })
			.SetParam(CANAME("constantColorBlock"), pConstantColor)
			.SetShaderInfo({ CAPATH("Shaders/Test/TestTriangleWithConstantColor") })
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
	auto scheduler = g_ThreadManager->NewScheduler();
	g_GPUBackend->ScheduleGPUFrame(scheduler.get(), newFrame);
}


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
	g_WindowSystem = windowSystemLoader.New();

	//Initialize Thread Manager
	g_ThreadManager = threadManagerLoader.New();
	unsigned int n = std::thread::hardware_concurrency();
	n = (n == 0) ? 5 : (castl::min)(n, 16u);
	g_ThreadManager->InitializeThreadCount(GetGlobalTimerSystem(), n);

	//Initialize IO Manager
	auto g_IOManager = ioManagerLoader.New();
	g_IOManager->Initialize(g_ThreadManager.get());

	//Resource System
	auto resourceSystemFactory = resourceSystemLoader.New();
	auto pResourceManagingSystem = resourceSystemFactory->NewManagingSystemShared();

	pResourceManagingSystem->Initialize(g_IOManager);
	pResourceManagingSystem->SetResourceRootPath(assetString);

	auto importingSystem = resourceSystemFactory->NewImportingSystemShared();
	importingSystem->SetResourceManager(pResourceManagingSystem.get());

	g_GPUBackend = renderBackendLoader.New();
	g_GPUBackend->Initialize(GetGlobalTimerSystem()
		, g_IOManager.get(), pResourceManagingSystem.get(), importingSystem.get()
		, "Test D3D12 Backend", "CASCADED Engine");
	importingSystem->ScanSourceDirectory(resourceString);


	//TestGPUGraph0();
	TestGPUGraph1();

	g_ThreadManager.reset();
	g_GPUBackend.reset();
	return EXIT_SUCCESS;
}
