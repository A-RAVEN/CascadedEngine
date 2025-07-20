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

void TestSimpleTriangle()
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

	while (!newWindow.lock()->WindowShouldClose())
	{
		g_WindowSystem->UpdateSystem();
		auto scheduler = g_ThreadManager->NewScheduler();
		g_GPUBackend->ScheduleGPUFrame(scheduler.get(), newFrame);
	}
}

void TestTriangleWithConstantColor()
{
	auto newWindow = g_WindowSystem->NewWindow(1024, 512, "Hello Triangle With Constant Color");
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
	pConstantColor->SetValue(CANAME("color"), glm::vec3(1.0f, 1.0f, 0.0f));

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

	while (!newWindow.lock()->WindowShouldClose())
	{
		g_WindowSystem->UpdateSystem();
		auto scheduler = g_ThreadManager->NewScheduler();
		g_GPUBackend->ScheduleGPUFrame(scheduler.get(), newFrame);
	}
}

void TestTriangleWithStructuredBufferColor()
{
	auto newWindow = g_WindowSystem->NewWindow(1024, 512, "Hello Triangle With Structured Color");
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

	BufferHandle structuredColorBuffer(CANAME("StructuredColorBuffer"));
	castl::shared_ptr<ShaderStruct> pStructuredColor = g_GPUBackend->CreateShaderStruct(CANAME("StructuredColor"));
	pStructuredColor->SetBuffer(CANAME("colorStructuredBuffer"), structuredColorBuffer);

	glm::vec3 testColor = glm::vec3(1.0f, 0.0f, 1.0f);

	ImageHandle windowBackBuffer(windowHandle);
	BufferHandle vbuffer(CANAME("TestVertBuffer"));
	castl::shared_ptr<GPUGraph> newGraph = castl::make_shared<GPUGraph>();
	newGraph->Present(windowBackBuffer)
		.AllocBuffer(vbuffer, GPUBufferDescriptor::Create(EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, testBuffer.size(), sizeof(testBuffer[0])))
		.ScheduleData(vbuffer, testBuffer.data(), testBuffer.size() * sizeof(testBuffer[0]))
		.AllocBuffer(structuredColorBuffer, GPUBufferDescriptor::Create(EBufferUsage::eStructuredBuffer | EBufferUsage::eDataDst, 1, sizeof(glm::vec3)))
		.ScheduleData(structuredColorBuffer, &testColor, sizeof(testColor))
		.AddPass(
			RenderPass::New({ windowBackBuffer })
			.SetParam(CANAME("structuredColorBlock"), pStructuredColor)
			.SetShaderInfo({ CAPATH("Shaders/Test/TestTriangleWithStructuredBufferColor") })
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
	while (!newWindow.lock()->WindowShouldClose())
	{
		g_WindowSystem->UpdateSystem();
		auto scheduler = g_ThreadManager->NewScheduler();
		g_GPUBackend->ScheduleGPUFrame(scheduler.get(), newFrame);
	}
}


void TestTriangleWithImageBuffer()
{
	auto newWindow = g_WindowSystem->NewWindow(1024, 512, "Hello Triangle");
	auto windowHandle = g_GPUBackend->GetWindowHandle(newWindow.lock());
	struct VertexStruct
	{
		std::array<float, 3> pos;
		std::array<float, 2> texcoord;
	};
	cacore::HashObj<VertexInputsDescriptor> descs = VertexInputsDescriptor::Create(sizeof(VertexStruct),
		{
			VertexAttribute::Create(offsetof(VertexStruct, pos), VertexInputFormat::eR32G32B32_SFloat, CANAME("POSITION")),
			VertexAttribute::Create(offsetof(VertexStruct, texcoord), VertexInputFormat::eR32G32_SFloat, CANAME("TEXCOORD")),
		}, false);

	std::vector<VertexStruct> testBuffer = {
		{{-0.25f, -0.25f, -0.25f }, {0.0f, 0.0f}},
		{{0.25f, -0.25f, -0.25f }, {1.0f, 0.0f}},
		{{0.0f, 0.5f, 0.0f }, {0.5f, 1.0f}},
	};

	ImageHandle windowBackBuffer(windowHandle);

	auto testTexture = g_GPUBackend->CreateGPUTexture(GPUTextureDescriptor::Create(256, 256
		, ETextureFormat::E_B8G8R8A8_UNORM
		, ETextureAccessType::eTransferDst | ETextureAccessType::eSampled));
	uint32_t color = (0 << 16) | (0 << 8) | (255);
	castl::vector<uint32_t> colorData(256 * 256, color);
	castl::shared_ptr<GPUGraph> submitGraph = castl::make_shared<GPUGraph>();
	submitGraph->ScheduleData(testTexture, colorData.data(), colorData.size() * sizeof(uint32_t));
	{
		auto scheduler = g_ThreadManager->NewScheduler();
		g_GPUBackend->ExecuteGraph(scheduler.get(), submitGraph);
	}

	auto imageStruct = g_GPUBackend->CreateShaderStruct(CANAME("SamplingTextureData"));
	imageStruct->SetImage(CANAME("testTexture"), testTexture);
	imageStruct->SetSampler(CANAME("testSampler"), TextureSamplerDescriptor::LinearClamp());

	BufferHandle vbuffer(CANAME("TestVertBuffer"));
	castl::shared_ptr<GPUGraph> newGraph = castl::make_shared<GPUGraph>();
	newGraph->Present(windowBackBuffer)
		.AllocBuffer(vbuffer, GPUBufferDescriptor::Create(EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, testBuffer.size(), sizeof(testBuffer[0])))
		.ScheduleData(vbuffer, testBuffer.data(), testBuffer.size() * sizeof(testBuffer[0]))
		.AddPass(
			RenderPass::New({ windowBackBuffer })
			.SetShaderInfo({ CAPATH("Shaders/Test/TestTriangleWithTextureSampling") })
			.SetParam(CANAME("textureData"), imageStruct)
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

	while (!newWindow.lock()->WindowShouldClose())
	{
		g_WindowSystem->UpdateSystem();
		auto scheduler = g_ThreadManager->NewScheduler();
		g_GPUBackend->ExecuteGraph(scheduler.get(), newGraph);
	}
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


	//TestSimpleTriangle();
	//TestTriangleWithConstantColor();
	//TestTriangleWithStructuredBufferColor();
	TestTriangleWithImageBuffer();

	g_ThreadManager.reset();
	g_GPUBackend.reset();
	return EXIT_SUCCESS;
}
