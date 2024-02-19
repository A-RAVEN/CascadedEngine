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
#include "TestShaderProvider.h"
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
using namespace thread_management;
using namespace library_loader;
using namespace graphics_backend;
using namespace ShaderCompiler;
using namespace uenum;
using namespace resource_management;


struct VertexData
{
	glm::vec2 pos;
	glm::vec2 uv;
	glm::vec3 color;
};

int main(int argc, char *argv[])
{
	std::filesystem::path rootPathFS{ "../../../../" , std::filesystem::path::format::native_format};
	std::filesystem::path rootPath = std::filesystem::absolute(rootPathFS);
	castl::string resourceString = castl::to_ca(rootPath.string()) + "CAResources";
	castl::string assetString = castl::to_ca(rootPath.string()) + "CAAssets";

	TModuleLoader<CThreadManager> threadManagerLoader("ThreadManager");
	TModuleLoader<CRenderBackend> renderBackendLoader("VulkanRenderBackend");
	TModuleLoader<RenderInterfaceManager> renderInterfaceLoader("Rendering");
	TModuleLoader<ResourceFactory> renderImportingSystemLoader("CAGeneralReourceSystem");
	auto resourceSystemFactory = renderImportingSystemLoader.New();

	ShaderConstantsBuilder shaderConstantBuilder{ "PerObjectConstants" };
	shaderConstantBuilder
		.Mat4<float>("ObjectMatrix");

	ShaderBindingBuilder shaderBindingBuilder{ "TestBinding" };
	shaderBindingBuilder.ConstantBuffer(shaderConstantBuilder);
	shaderBindingBuilder.Texture2D<float, 1>("TestTexture");
	shaderBindingBuilder.SamplerState("TestSampler");

	ShaderBindingBuilder finalBlitBindingBuilder{ "FinalBlitBinding" };
	finalBlitBindingBuilder.Texture2D<float, 4>("SourceTexture");
	finalBlitBindingBuilder.SamplerState("SourceSampler");

	auto pThreadManager = threadManagerLoader.New();
	pThreadManager->InitializeThreadCount(5);

	auto pRenderInterface = renderInterfaceLoader.New();

	ShaderResourceLoaderSlang slangShaderResourceLoader;
	StaticMeshImporter staticMeshImporter;

	auto pResourceImportingSystem = resourceSystemFactory->NewImportingSystemShared();
	auto pResourceManagingSystem = resourceSystemFactory->NewManagingSystemShared();
	pResourceManagingSystem->SetResourceRootPath(assetString);
	pResourceImportingSystem->SetResourceManager(pResourceManagingSystem.get());
	pResourceImportingSystem->AddImporter(&slangShaderResourceLoader);
	pResourceImportingSystem->AddImporter(&staticMeshImporter);
	pResourceImportingSystem->ScanSourceDirectory(resourceString);


	ShaderResrouce* pGeneralShaderResource = nullptr;
	pResourceManagingSystem->LoadResource<ShaderResrouce>("Shaders/TestStaticMeshShader.shaderbundle", [ppResource = &pGeneralShaderResource](ShaderResrouce* result)
		{
			*ppResource = result;
		});

	ShaderResrouce* pFinalBlitShaderResource = nullptr;
	pResourceManagingSystem->LoadResource<ShaderResrouce>("Shaders/testFinalBlit.shaderbundle", [ppResource = &pFinalBlitShaderResource](ShaderResrouce* result)
		{
			*ppResource = result;
		});

	StaticMeshResource* pTestMeshResource = nullptr;
	pResourceManagingSystem->LoadResource<StaticMeshResource>("Models/VikingRoom/mesh.scene", [ppResource = &pTestMeshResource](StaticMeshResource* result)
		{
			*ppResource = result;
		});

	TextureResource* pTextureResource0 = nullptr;
	pResourceManagingSystem->LoadResource<TextureResource>("Models/VikingRoom/IMG_2348.texture", [ppResource = &pTextureResource0](TextureResource* result)
		{
			*ppResource = result;
		});

	TextureResource* pTextureResource1 = nullptr;
	pResourceManagingSystem->LoadResource<TextureResource>("Models/VikingRoom/IMG_2349.texture", [ppResource = &pTextureResource1](TextureResource* result)
		{
			*ppResource = result;
		});



	//GraphicsShaderSet shaderSet{};
	//shaderSet.vert = &pTestShaderResource->m_VertexShaderProvider;
	//shaderSet.frag = &pTestShaderResource->m_FragmentShaderProvider;

	GraphicsShaderSet finalBlitShaderSet{};
	finalBlitShaderSet.vert = &pFinalBlitShaderResource->m_VertexShaderProvider;
	finalBlitShaderSet.frag = &pFinalBlitShaderResource->m_FragmentShaderProvider;



	auto pBackend = renderBackendLoader.New();
	pBackend->Initialize("Test Vulkan Backend", "CASCADED Engine");
	pBackend->InitializeThreadContextCount(5);

	IMGUIContext imguiContext{};
	imguiContext.Initialize(pBackend.get(), pResourceManagingSystem.get());

	auto sampler = pBackend->GetOrCreateTextureSampler(TextureSamplerDescriptor{});

	auto texture0 = pBackend->CreateGPUTexture(GPUTextureDescriptor{
		pTextureResource0->GetWidth()
		, pTextureResource0->GetHeight()
		, pTextureResource0->GetFormat()
		, ETextureAccessType::eSampled | ETextureAccessType::eTransferDst
		});
	texture0->ScheduleTextureData(0, pTextureResource0->GetDataSize(), pTextureResource0->GetData());

	auto texture1 = pBackend->CreateGPUTexture(GPUTextureDescriptor{
	pTextureResource1->GetWidth()
	, pTextureResource1->GetHeight()
	, pTextureResource1->GetFormat()
	, ETextureAccessType::eSampled | ETextureAccessType::eTransferDst
		});
	texture1->ScheduleTextureData(0, pTextureResource1->GetDataSize(), pTextureResource1->GetData());

	auto samplingTextureBinding0 = pBackend->CreateShaderBindingSet(finalBlitBindingBuilder);
	samplingTextureBinding0->SetName("samplingTextureBinding0");
	samplingTextureBinding0->SetTexture("SourceTexture", texture0);
	samplingTextureBinding0->SetSampler("SourceSampler", sampler);

	auto samplingTextureBinding1 = pBackend->CreateShaderBindingSet(finalBlitBindingBuilder);
	samplingTextureBinding1->SetName("samplingTextureBinding1");
	samplingTextureBinding1->SetTexture("SourceTexture", texture1);
	samplingTextureBinding1->SetSampler("SourceSampler", sampler);

	auto windowHandle = pBackend->NewWindow(1024, 512, "Test Window2");

	{
		MeshGPUData newMeshGPUData{ pBackend };
		newMeshGPUData.UploadMeshResource(pTestMeshResource, "VikingScene");
		g_MeshResourceToGPUData.insert(castl::make_pair(pTestMeshResource, newMeshGPUData));
	}

	MeshBatchDrawInterface drawInterface{};
	{
		MeshRenderer meshRenderer{};
		meshRenderer.p_MeshResource = pTestMeshResource;
		meshRenderer.materials = {
			MeshMaterial 
			{
				CPipelineStateObject{ DepthStencilStates::NormalOpaque() }
				, { &pGeneralShaderResource->m_VertexShaderProvider, &pGeneralShaderResource->m_FragmentShaderProvider }
				, { samplingTextureBinding1 }
			},
			MeshMaterial 
			{
				CPipelineStateObject{ DepthStencilStates::NormalOpaque() }
				, { &pGeneralShaderResource->m_VertexShaderProvider, &pGeneralShaderResource->m_FragmentShaderProvider }
				, { samplingTextureBinding0 }
			}
		};
		drawInterface.AddMesh(meshRenderer, glm::mat4(1.0f));

		drawInterface.MakeBatch(pBackend.get());
	}


	castl::vector<VertexData> vertexDataList = {
		VertexData{glm::vec2(-1.0f, -1.0f), glm::vec2(0.0f, 0.0f), glm::vec3(1, 1, 1)},
		VertexData{glm::vec2(1.0f, -1.0f), glm::vec2(1.0f, 0.0f), glm::vec3(1, 1, 1)},
		VertexData{glm::vec2(1.0f, 1.0f), glm::vec2(1.0f, 1.0f), glm::vec3(1, 1, 1)},
		VertexData{glm::vec2(-1.0f, 1.0f), glm::vec2(0.0f, 1.0f), glm::vec3(1, 1, 1)},
	};

	castl::vector<uint16_t> indexDataList = {
		0, 3, 2, 2, 1, 0
	};


	auto vertexBuffer = pBackend->CreateGPUBuffer(
		EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, vertexDataList.size(), sizeof(VertexData));
	vertexBuffer->ScheduleBufferData(0, vertexDataList.size() * sizeof(VertexData), vertexDataList.data());


	auto indexBuffer = pBackend->CreateGPUBuffer(
		EBufferUsage::eIndexBuffer | EBufferUsage::eDataDst, indexDataList.size(), sizeof(uint16_t));
	indexBuffer->ScheduleBufferData(0, indexDataList.size() * sizeof(uint16_t), indexDataList.data());

	uint64_t frame = 0;

	CVertexInputDescriptor vertexInputDesc{};
	vertexInputDesc.AddPrimitiveDescriptor(sizeof(VertexData), {
		VertexAttribute{0, offsetof(VertexData, pos), VertexInputFormat::eR32G32_SFloat}
		, VertexAttribute{1, offsetof(VertexData, uv), VertexInputFormat::eR32G32_SFloat}
		, VertexAttribute{2, offsetof(VertexData, color), VertexInputFormat::eR32G32B32_SFloat}
		});

	ShaderBindingDescriptorList bindingSetList = { shaderBindingBuilder };
	
	castl::chrono::high_resolution_clock timer;
	auto lastTime = timer.now();
	float deltaTime = 0.0f;
	Camera cam;
	bool mouseDown = false;
	glm::vec2 lastMousePos = {0.0f, 0.0f};

	std::shared_future<void> m_TaskFuture;
	auto graphicsTaskGraph = pThreadManager->NewTaskGraph()
		->Name("Graphics Task Graph");
	pBackend->SetupGraphicsTaskGraph(graphicsTaskGraph, {}, frame);
	m_TaskFuture = graphicsTaskGraph->Run();
	m_TaskFuture.wait();
	pThreadManager->SetupFunction([
				pBackend
				, pTimer = &timer
				, pFrame = &frame
				, pLastTime = &lastTime
				, pThreadManager
				, windowHandle
				, pRenderInterface
				, sampler
				, pCam = &cam
				, pmouseDown = &mouseDown
				, pdrawInterface = &drawInterface
				, plastMousePos = &lastMousePos
				, pDeltaTime = &deltaTime
				, pVertexList = &vertexDataList
				, pIndexList = &indexDataList
				, pFinalBlitShaderSet = &finalBlitShaderSet
				, pVertexInputDesc = &vertexInputDesc
				, pFinalBlitBindingDesc = &finalBlitBindingBuilder
				, pImguiContext = &imguiContext
		](CThreadManager* thisManager)
		{
			if (!pBackend->AnyWindowRunning())
				return false;

			{
				uint64_t lastFrame = *pFrame == 0 ? 0 : (*pFrame - 1);
				uint64_t currentFrame = *pFrame;

				auto baseTaskGraph = pThreadManager->NewTaskGraph()
					->Name("BaseTaskGraph");
				auto gamePlayGraph = baseTaskGraph->NewTaskGraph()
					->Name("GamePlayGraph")
					->WaitOnEvent("GamePlay", lastFrame)
					->SignalEvent("GamePlay", currentFrame);

				auto updateImGUIGraph = gamePlayGraph->NewTask()
					->Name("Update Imgui")
					->Functor([windowHandle, pImguiContext]()
						{
							pImguiContext->UpdateIMGUI(windowHandle.get());
						});

				auto updateCameraGraph = gamePlayGraph->NewTask()
					->Name("Update Camera")
					->Functor([plastMousePos
						, pCam
						, pmouseDown
						, pdrawInterface
						, pDeltaTime
						, windowHandle]()
						{
							int forwarding = 0;
							int lefting = 0;
							if (windowHandle->IsKeyDown(CA_KEY_W))
							{
								++forwarding;
							}
							if (windowHandle->IsKeyDown(CA_KEY_S))
							{
								--forwarding;
							}
							if (windowHandle->IsKeyDown(CA_KEY_A))
							{
								++lefting;
							}
							if (windowHandle->IsKeyDown(CA_KEY_D))
							{
								--lefting;
							}
							if (windowHandle->IsKeyTriggered(CA_KEY_R))
							{
								windowHandle->RecreateContext();
							}
							glm::vec2 mouseDelta = { 0.0f, 0.0f };
							glm::vec2 mousePos = { windowHandle->GetMouseX(),windowHandle->GetMouseY() };
							if (windowHandle->IsMouseDown(CA_MOUSE_BUTTON_LEFT))
							{
								//castl::cout << "Mouse Down!" << castl::endl;
								if (!*pmouseDown)
								{
									*plastMousePos = mousePos;
									*pmouseDown = true;
								}
								mouseDelta = mousePos - *plastMousePos;
								*plastMousePos = mousePos;
							}
							else// if(windowHandle->IsMouseUp(CA_MOUSE_BUTTON_LEFT))
							{
								*pmouseDown = false;
							}

							auto windowSize1 = windowHandle->GetSizeSafe();
							pCam->Tick(*pDeltaTime, forwarding, lefting, mouseDelta.x, mouseDelta.y, windowSize1.x, windowSize1.y);
							pdrawInterface->Update(pCam->GetViewProjMatrix());
						});

				auto grapicsTaskGraph = baseTaskGraph->NewTaskGraph()
					->Name("Graphics Task Graph")
					->DependsOn(gamePlayGraph)
					->WaitOnEvent("Graphics", lastFrame)
					->SignalEvent("Graphics", currentFrame);

				castl::shared_ptr<castl::vector<castl::shared_ptr<CRenderGraph>>> pGraphs = castl::make_shared<castl::vector<castl::shared_ptr<CRenderGraph>>>();

				auto setupRGTask = grapicsTaskGraph->NewTask()
					->Name("Setup Render Graph")
					->Functor([pBackend
						, pRenderInterface
						, windowHandle
						, pdrawInterface
						, pVertexList
						, pIndexList
						, pFinalBlitShaderSet
						, pVertexInputDesc
						, pFinalBlitBindingDesc
						, pImguiContext
						, sampler
						, currentFrame
						, pGraphs
					]()
				{
					auto windowSize = windowHandle->GetSizeSafe();
					CA_ASSERT(windowSize.x <= 8192 && windowSize.y <= 8192, "invalid window size "
						+ castl::to_ca(std::to_string(windowSize.x)) + "x"
						+ castl::to_ca(std::to_string(windowSize.y)));
				/*	castl::cout << "Setup Size " << currentFrame <<  ":" << windowSize.x << "x" << windowSize.y << castl::endl;
					castl::cout << "RT Size " << currentFrame <<  ":" << windowHandle->GetBackbufferDescriptor().width << "x" << windowHandle->GetBackbufferDescriptor().height << castl::endl;*/

					auto pRenderGraph = pRenderInterface->NewRenderGraph();
					auto windowBackBuffer = pRenderGraph->RegisterWindowBackbuffer(windowHandle.get());

					auto depthTextureHandle = pRenderGraph->NewTextureHandle(GPUTextureDescriptor{
						windowSize.x, windowSize.y
						, ETextureFormat::E_D32_SFLOAT
						, ETextureAccessType::eRT | ETextureAccessType::eSampled });

					auto colorTextureHandle = pRenderGraph->NewTextureHandle(GPUTextureDescriptor{
						windowSize.x, windowSize.y
						, ETextureFormat::E_R8G8B8A8_UNORM
						, ETextureAccessType::eRT | ETextureAccessType::eSampled });

					pRenderGraph->NewRenderPass({ 
						CAttachmentInfo::Make(colorTextureHandle, GraphicsClearValue::ClearColor(0.0f, 1.0f, 1.0f, 1.0f))
						, CAttachmentInfo::Make(depthTextureHandle, GraphicsClearValue::ClearDepthStencil(1.0f, 0x0))
						})
						.SetAttachmentTarget(0, colorTextureHandle)
						.SetAttachmentTarget(1, depthTextureHandle)
						.Subpass({ {0}, 1 }
							, {}
							, pdrawInterface
						);

					auto vertexBufferHandle = pRenderGraph->NewGPUBufferHandle(EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, pVertexList->size(), sizeof(VertexData))
						.ScheduleBufferData(0, pVertexList->size() * sizeof(VertexData), pVertexList->data());
					auto indexBufferHandle = pRenderGraph->NewGPUBufferHandle(EBufferUsage::eIndexBuffer | EBufferUsage::eDataDst, pIndexList->size(), sizeof(uint16_t))
						.ScheduleBufferData(0, pIndexList->size() * sizeof(uint16_t), pIndexList->data());

					ShaderBindingSetHandle blitBandingHandle = pRenderGraph->NewShaderBindingSetHandle(*pFinalBlitBindingDesc)
						.SetTexture("SourceTexture", colorTextureHandle)
						.SetSampler("SourceSampler", sampler);

					pRenderGraph->NewRenderPass(windowBackBuffer, GraphicsClearValue::ClearColor(0.0f, 1.0f, 1.0f, 1.0f)
						, CPipelineStateObject{ {}, {RasterizerStates::CullOff()} }
						, * pVertexInputDesc
						, * pFinalBlitShaderSet
						, { {}, {blitBandingHandle} }
						, [blitBandingHandle, vertexBufferHandle, indexBufferHandle](CInlineCommandList& cmd)
						{
							cmd.SetShaderBindings(castl::vector<ShaderBindingSetHandle>{ blitBandingHandle })
								.BindVertexBuffers({ vertexBufferHandle }, {})
								.BindIndexBuffers(EIndexBufferType::e16, indexBufferHandle)
								.DrawIndexed(6);
						});

					pImguiContext->DrawIMGUI(pRenderGraph.get(), windowBackBuffer);

					pGraphs->push_back(pRenderGraph);
				});

				grapicsTaskGraph->NewTaskGraph()
					->Name("Submit Backend Task Graph")
					->DependsOn(setupRGTask)
					->SetupFunctor([pBackend, pGraphs, currentFrame](CTaskGraph* thisGraph)
						{
							pBackend->SetupGraphicsTaskGraph(
								thisGraph, *pGraphs, currentFrame);
						});

				baseTaskGraph->Run();

				++*pFrame;
				auto currentTime = pTimer->now();
				auto duration = castl::chrono::duration_cast<castl::chrono::milliseconds>(currentTime - *pLastTime).count();
				*pLastTime = currentTime;
				*pDeltaTime = duration / 1000.0f;
				*pDeltaTime = castl::max(*pDeltaTime, 0.0001f);
				float frameRate = 1.0f / *pDeltaTime;
				//castl::cout << "Frame Rate: " << frameRate << castl::endl;
			}
			return true;
		}, "Graphics");
	pThreadManager->RunSetupFunction();
	pThreadManager->LogStatus();
	imguiContext.Release();
	pBackend->Release();
	pBackend.reset();
	pThreadManager.reset();
	return EXIT_SUCCESS;
}
