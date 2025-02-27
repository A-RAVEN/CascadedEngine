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
#include <chrono>
#include <filesystem>
#include <CASTL/CAString.h>
#include <CASTL/CAChrono.h>
#include "Camera.h"
#include "KeyCodes.h"
#include <CAResource/ResourceSystemFactory.h>
#include "ShaderResource.h"
#include "StaticMeshResource.h"
#include "MeshRenderer.h"
#include "TextureResource.h"
#include "IMGUIContext.h"
#include <GPUGraph.h>
#include <TextureSampler.h>
#include <CAWindow/WindowSystem.h>
#include "MiniDump.h"
//#include "Profiler.h"
#include <CATimer/Timer.h>
#include <TimerSystemEditor/TimerSystem_Impl.h>
#include <thread>
#include <IOManager/IOManager.h>
using namespace thread_management;
using namespace library_loader;
using namespace graphics_backend;
using namespace uenum;
using namespace resource_management;
using namespace cawindow;
using namespace catimer;
using namespace ca_io;

castl::shared_ptr<IOManager> g_IOManager;


struct VertexData
{
	glm::vec2 pos;
	glm::vec2 uv;
	glm::vec3 color;
};

int main(int argc, char *argv[])
{
	MiniDump::EnableAutoDump(true);

	std::filesystem::path rootPathFS{ "../../../../" , std::filesystem::path::format::native_format};
	std::filesystem::path rootPath = std::filesystem::absolute(rootPathFS);
	castl::string resourceString = castl::to_ca(rootPath.string()) + "CAResources";
	castl::string assetString = castl::to_ca(rootPath.string()) + "CAAssets";
	castl::string editorResourceString = castl::to_ca(rootPath.string()) + "EditorConfigs";

	TModuleLoader<CThreadManager> threadManagerLoader("ThreadManager");
	TModuleLoader<CRenderBackend> renderBackendLoader("VulkanRenderBackend");
	TModuleLoader<ResourceFactory> resourceSystemLoader("CAGeneralReourceSystem");
	TModuleLoader<IWindowSystem> windowSystemLoader("WindowSystem");
	TModuleLoader<IOManager> ioManagerLoader("IOManager_FS");


	auto windowSystem = windowSystemLoader.New();

	constexpr castl::string_view testString = "Test";
	castl::cout << testString.size() << castl::endl;
	cacore::NameHash hash0 = CANAME("Test");
	cacore::NameHash hash1 = "Test";

	InitTimerSystem();
	GetGlobalTimerSystem()->SetThreadName("Main");

	TIMER_NEWFRAME();
	//gCPUProfiler.Initialize(5, 1024);
	//PROFILE_FRAME();

	auto resourceSystemFactory = resourceSystemLoader.New();
	auto pThreadManager = threadManagerLoader.New();
	unsigned int n = std::thread::hardware_concurrency();
	n = (n == 0) ? 5 : (castl::min)(n, 16u);
	pThreadManager->InitializeThreadCount(GetGlobalTimerSystem(), n);

	g_IOManager = ioManagerLoader.New();
	g_IOManager->Initialize(pThreadManager.get());


	ShaderResourceLoaderSlang slangShaderResourceLoader;
	StaticMeshImporter staticMeshImporter;


	auto pResourceManagingSystem = resourceSystemFactory->NewManagingSystemShared();
	pResourceManagingSystem->Initialize(g_IOManager);
	pResourceManagingSystem->SetResourceRootPath(assetString);

	auto pResourceImportingSystem = resourceSystemFactory->NewImportingSystemShared();
	pResourceImportingSystem->SetResourceManager(pResourceManagingSystem.get());
	pResourceImportingSystem->AddImporter(&slangShaderResourceLoader);
	pResourceImportingSystem->AddImporter(&staticMeshImporter);
	auto pBackend = renderBackendLoader.New();
	pBackend->Initialize(GetGlobalTimerSystem(), g_IOManager.get(), pResourceManagingSystem.get(), pResourceImportingSystem.get(), "Test Vulkan Backend", "CASCADED Engine");
	pBackend->RunTestCode();
	pResourceImportingSystem->ScanSourceDirectory(resourceString);


	auto testData1 = pBackend->CreateShaderStruct(CANAME("TestData.TestDataStruct1"));
	auto testData = pBackend->CreateShaderStruct(CANAME("TestData.TestDataStruct"));
	testData->SetStruct(CANAME("testSubBlock"), testData1, 0);
	testData->SetStruct(CANAME("testSubBlock"), testData1, 1);

	//auto pMeshShaderResource = pResourceManagingSystem->GetOrLoadResource<ShaderResrouce>("Shaders/TestStaticMeshShader.shaderbundle");

	//auto pFinalBlitShaderResource = pResourceManagingSystem->GetOrLoadResource<ShaderResrouce>("Shaders/testFinalBlit.shaderbundle");

	//auto pTestComputeShaderResource = pResourceManagingSystem->GetOrLoadResource<ShaderResrouce>("Shaders/TestComputeShader.shaderbundle");

	//auto pFinalBlitShader = pResourceManagingSystem->GetOrLoadResource<ShaderResrouce>("Shaders/FinalBlit.shaderbundle");

	auto pTestMeshResource = pResourceManagingSystem->GetOrLoadResource<StaticMeshResource>("Models/VikingRoom/mesh.scene");

	auto pTextureResource0 = pResourceManagingSystem->GetOrLoadResource<TextureResource>("Models/VikingRoom/IMG_2348.texture");

	auto pTextureResource1 = pResourceManagingSystem->GetOrLoadResource<TextureResource>("Models/VikingRoom/IMG_2349.texture");



	imgui_display::IMGUIContext imguiContext;

	auto newWindow = windowSystem->NewWindow(1024, 512, "Window System Window");
	auto windowHandle = pBackend->GetWindowHandle(newWindow.lock());


	castl::vector<VertexData> vertexDataList = {
		VertexData{glm::vec2(-1.0f, -1.0f), glm::vec2(0.0f, 0.0f), glm::vec3(1, 1, 1)},
		VertexData{glm::vec2(1.0f, -1.0f), glm::vec2(1.0f, 0.0f), glm::vec3(1, 1, 1)},
		VertexData{glm::vec2(1.0f, 1.0f), glm::vec2(1.0f, 1.0f), glm::vec3(1, 1, 1)},
		VertexData{glm::vec2(-1.0f, 1.0f), glm::vec2(0.0f, 1.0f), glm::vec3(1, 1, 1)},
	};

	castl::vector<uint16_t> indexDataList = {
		0, 1, 2, 2, 3, 0
	};

	auto vertexBuffer = pBackend->CreateGPUBuffer(
		EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, vertexDataList.size(), sizeof(VertexData));


	auto indexBuffer = pBackend->CreateGPUBuffer(
		EBufferUsage::eIndexBuffer | EBufferUsage::eDataDst, indexDataList.size(), sizeof(uint16_t));

	auto texture = pBackend->CreateGPUTexture(GPUTextureDescriptor::Create(pTextureResource0->GetWidth(), pTextureResource0->GetHeight(), pTextureResource0->GetFormat(), ETextureAccessType::eSampled | ETextureAccessType::eTransferDst));
	auto texture1 = pBackend->CreateGPUTexture(GPUTextureDescriptor::Create(pTextureResource1->GetWidth(), pTextureResource1->GetHeight(), pTextureResource1->GetFormat(), ETextureAccessType::eSampled | ETextureAccessType::eTransferDst));

	{
		auto pSetupScheduler = pThreadManager->NewScheduler();
		pSetupScheduler->NewTaskGraph()
			->Name("Setup")
			->Func([&, pBackend = pBackend](auto scheduler)
			{
				castl::shared_ptr<GPUGraph> submitGraph = castl::make_shared<GPUGraph>();
				submitGraph->ScheduleData(BufferHandle{ vertexBuffer }, vertexDataList.data(), vertexDataList.size() * sizeof(vertexDataList[0]));
				submitGraph->ScheduleData(BufferHandle{ indexBuffer }, indexDataList.data(), indexDataList.size() * sizeof(indexDataList[0]));
				submitGraph->ScheduleData(texture, pTextureResource0->GetData(), pTextureResource0->GetDataSize());
				submitGraph->ScheduleData(texture1, pTextureResource1->GetData(), pTextureResource1->GetDataSize());
				RegisterMeshResource(pBackend, submitGraph.get(), pTestMeshResource.get());

				imguiContext.Initialize(editorResourceString, pBackend, windowSystem, newWindow.lock(), pResourceManagingSystem.get(), submitGraph.get());

				GPUFrame submitFrame{};
				submitFrame.pGraph = submitGraph;
				pBackend->ScheduleGPUFrame(scheduler, submitFrame);
			});
	}

	VertexInputsDescriptor vertexInputDesc = VertexInputsDescriptor::Create(
		sizeof(VertexData),
		{
			VertexAttribute::Create(offsetof(VertexData, pos), VertexInputFormat::eR32G32_SFloat, "POSITION"),
			VertexAttribute::Create(offsetof(VertexData, uv), VertexInputFormat::eR32G32_SFloat, "TEXCOORD", 0),
			VertexAttribute::Create(offsetof(VertexData, color), VertexInputFormat::eR32G32B32_SFloat, "COLOR"),
		}
	);

	MeshMaterial meshMaterial0;
	meshMaterial0.pipelineStateObject = { 
		DepthStencilStates::NormalOpaque()
		, RasterizerStates::CullBack()
	};
	meshMaterial0.shaderStruct = pBackend->CreateShaderStruct(CANAME("MeshMaterial"));
	meshMaterial0.shaderStruct->SetImage(CANAME("albedoTexture"), texture1)
		.SetSampler(CANAME("sampler"), TextureSamplerDescriptor::Create());
	meshMaterial0.shaderSet = { "Shaders/TestStaticMeshShader" };

	MeshMaterial meshMaterial1;
	meshMaterial1.pipelineStateObject = {
		DepthStencilStates::NormalOpaque()
		, RasterizerStates::CullBack()
	};
	meshMaterial1.shaderStruct = pBackend->CreateShaderStruct(CANAME("MeshMaterial"));
	meshMaterial1.shaderStruct->SetImage(CANAME("albedoTexture"), texture)
		.SetSampler(CANAME("sampler"), TextureSamplerDescriptor::Create());
	meshMaterial1.shaderSet = { "Shaders/TestStaticMeshShader" };


	MeshRenderer meshRenderer{};
	meshRenderer.p_MeshResource = pTestMeshResource.get();
	meshRenderer.materials.resize(2);
	meshRenderer.materials[0] = meshMaterial0;
	meshRenderer.materials[1] = meshMaterial1;

	MeshBatcher meshBatcher{ pBackend };
	meshBatcher.AddMeshRenderer(meshRenderer, glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));

	Camera camera;
	castl::chrono::high_resolution_clock timer;
	auto lastTime = timer.now();
	float deltaTime = 0.0f;
	Camera cam;
	bool mouseDown = false;
	glm::vec2 lastMousePos = { 0.0f, 0.0f };

	castl::shared_ptr<ShaderStruct> globalLightParams = pBackend->CreateShaderStruct(CANAME("GlobalDirectLighting"));
	globalLightParams->SetValue(CANAME("lightDirection"), glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)))
		.SetValue(CANAME("lightColor"), glm::vec3(2.0f, 2.0f, 0.25f))
		.SetValue(CANAME("ambientColor"), glm::vec3(0.1f, 0.1f, 0.2f));

	
	pThreadManager->LoopFunction([&](auto setup)
	{
		TIMER_NEWFRAME();
		//setup->MainThread();
		//PROFILE_CPU_SCOPE("MainThread Setup");
		auto updateWindow = setup->NewTask()
			->MainThread()
			->Functor(
				[&]()
				{
					CPUTIMER_SCOPE("Update Window");
					windowSystem->UpdateSystem();
					imguiContext.UpdateIMGUI();
				});
		if (newWindow.expired())
		{
			return;
		}

		castl::shared_ptr<GPUGraph> newGraph = castl::make_shared<GPUGraph>();

		auto graphics = setup->NewTaskGraph()
			->Name("DrawEverything")
			->DependsOn(updateWindow)
			->MainThread()
			->Func([&, newGraph](auto scheduler)
				{

					CPUTIMER_SCOPE("Draw Everything");
					//IMGUI Logic
					imguiContext.PrepareDrawData(newGraph.get());
					//Draw Viewports Here
#pragma region DrawLoop
					auto& viewContexts = imguiContext.GetTextureViewContexts();

					int forwarding = 0;
					int lefting = 0;
					glm::vec2 mouseDelta = { 0.0f, 0.0f };
					auto locked = newWindow.lock();

					for (auto& view : viewContexts)
					{
						if (view.m_WindowHandle == nullptr)
							continue;
						if (view.m_WindowHandle->GetWindowFocus())
						{
							auto lockedWindow = view.m_WindowHandle;
							if (lockedWindow->IsKeyDown(CA_KEY_W))
							{
								++forwarding;
							}
							if (lockedWindow->IsKeyDown(CA_KEY_S))
							{
								--forwarding;
							}
							if (lockedWindow->IsKeyDown(CA_KEY_A))
							{
								++lefting;
							}
							if (lockedWindow->IsKeyDown(CA_KEY_D))
							{
								--lefting;
							}
							glm::vec2 mousePos = { lockedWindow->GetMouseX(),lockedWindow->GetMouseY() };
							if (lockedWindow->IsMouseDown(CA_MOUSE_BUTTON_LEFT))
							{
								if (!mouseDown)
								{
									lastMousePos = mousePos;
									mouseDown = true;
								}
								mouseDelta = mousePos - lastMousePos;
								lastMousePos = mousePos;
							}
							else
							{
								mouseDown = false;
							}
						}
					}

					for(auto& viewContext : viewContexts)
					{
						CPUTIMER_SCOPE("Draw View");

						if (!viewContext.m_RenderTarget.IsValid())
						{
							continue;
						}
						auto windowSize1 = glm::vec2{ viewContext.m_ViewportRect.width, viewContext.m_ViewportRect.height };
						camera.Tick(deltaTime, forwarding, lefting, mouseDelta.x, mouseDelta.y, windowSize1.x, windowSize1.y);
						auto currentTime = timer.now();
						auto duration = castl::chrono::duration_cast<castl::chrono::milliseconds>(currentTime - lastTime).count();
						lastTime = currentTime;
						deltaTime = duration / 1000.0f;
						deltaTime = castl::max(deltaTime, 0.0001f);
						float frameRate = 1.0f / deltaTime;

						//castl::shared_ptr<ShaderArgList> cameraArgList = castl::make_shared<ShaderArgList>();
						auto cameraArgs = pBackend->CreateShaderStruct(CANAME("CameraData"));
						auto viewMatrix = glm::transpose(camera.GetViewProjMatrix());
						cameraArgs->SetValue(CANAME("viewProjMatrix"), viewMatrix);

						ImageHandle colorTexture{ "ColorTexture" };
						ImageHandle depthTexture{ "DepthTexture" };
						auto colorTextureDesc = viewContext.m_TextureDescriptor;
						colorTextureDesc.accessType = ETextureAccessType::eRT | ETextureAccessType::eSampled;
						auto depthTextureDesc = colorTextureDesc;
						depthTextureDesc.format = ETextureFormat::E_D32_SFLOAT;
						newGraph->
							AllocImage(colorTexture, colorTextureDesc)
							.AllocImage(depthTexture, depthTextureDesc);
						auto finalBlitShaderArgList = pBackend->CreateShaderStruct("FinalBlitInputs");
						finalBlitShaderArgList->SetImage(CANAME("SourceTexture"), colorTexture, GPUTextureView::CreateDefaultForSampling(viewContext.m_TextureDescriptor.format));
						finalBlitShaderArgList->SetSampler(CANAME("SourceSampler"), TextureSamplerDescriptor::Create());
						RenderPass drawMeshRenderPass = RenderPass::New(colorTexture, depthTexture
							, AttachmentConfig::Clear()
							, AttachmentConfig::ClearDepthStencil())
							.SetParam("cameraData", cameraArgs)
							.SetParam("globalLighting", globalLightParams);
						meshBatcher.Draw(newGraph.get(), &drawMeshRenderPass);
						newGraph->AddPass(drawMeshRenderPass)
							.AddPass
							(
								RenderPass::New(viewContext.m_RenderTarget)
								.SetPipelineState({})
								.SetParam("finalBlitInputs", finalBlitShaderArgList)
								.SetShaderInfo({ "Shaders/FinalBlit" })
								.DrawCall
								(
									DrawCallBatch::New()
									.VertexStream(CANAME("BlitVertex"), vertexInputDesc)
									.DrawCall
									(
										DrawCall::New()
										.SetVertexBuffer(CANAME("BlitVertex"), vertexBuffer)
										.SetIndexBuffer(EIndexBufferType::e16, indexBuffer, 0)
										.DrawIndexed(6)
									)
								)
							);
					}

#pragma endregion
					imguiContext.Draw(newGraph.get());
				});

		setup->NewTaskGraph()
			->Name("Submit GPU")
			->DependsOn(graphics)
			->Func([&, newGraph](auto scheduler)
				{
					CPUTIMER_SCOPE("Submit GPUGraph");
					auto& presentSurfaces = imguiContext.GetWindowHandles();
					GPUFrame gpuFrame{};
					gpuFrame.pGraph = newGraph;
					for (auto& surface : presentSurfaces)
					{
						gpuFrame.presentWindows.push_back(surface);
					}
					pBackend->ScheduleGPUFrame(scheduler, gpuFrame);
					scheduler->WaitAll();
				});
	}, "FullGraph");
	pThreadManager->Run();
	pThreadManager->LogStatus();
	pThreadManager.reset();
	pBackend->Release();
	pBackend.reset();

	//gCPUProfiler.Shutdown();
	return EXIT_SUCCESS;
}
