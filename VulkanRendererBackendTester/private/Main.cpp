#include <cstdlib>
#define NOMINMAX
#include <windows.h>
#include <ThreadManager/header/ThreadManager.h>
#include <ShaderCompiler/header/Compiler.h>
#include <iostream>
#include <CACore/header/library_loader.h>
#include <RenderInterface/header/CRenderBackend.h>
#include <RenderInterface/header/RenderInterfaceManager.h>
#include <RenderInterface/header/CNativeRenderPassInfo.h>
#include <RenderInterface/header/CCommandList.h>
#include <RenderInterface/header/ShaderBindingBuilder.h>
#include <CACore/header/FileLoader.h>
#include "TestShaderProvider.h"
#include <ExternalLib/glm/glm/mat4x4.hpp>
#include <ExternalLib/glm/glm/gtc/matrix_transform.hpp>
#include <ExternalLib/stb/stb_image.h>
#include <chrono>
#include <filesystem>
#include "Camera.h"
#include "KeyCodes.h"
#include <ExternalLib/imgui/imgui.h>
#include <GeneralResources/header/ResourceSystemFactory.h>
#include "ShaderResource.h"
#include "StaticMeshResource.h"
#include "MeshRenderer.h"
#include "TextureResource.h"
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

void UpdateIMGUI(int width, int height)
{
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(width, height);
	io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

	ImGui::NewFrame();
	//SRS - ShowDemoWindow() sets its own initial position and size, cannot override here
	ImGui::ShowDemoWindow();

	// Render to generate draw buffers
	ImGui::Render();
}

void DrawIMGUI(std::shared_ptr<CRenderGraph> renderGraph
	, TextureHandle framebufferHandle
	, GraphicsShaderSet shaderSet
	, ShaderConstantsBuilder const& imguiConstantBuilder
	, ShaderBindingBuilder const& imguiShaderBindingBuilder
	, std::shared_ptr<TextureSampler> sampler
	, std::shared_ptr<GPUTexture> fontTexture)
{
	ImGuiIO& io = ImGui::GetIO();
	glm::vec2 meshScale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
	ImDrawData* imDrawData = ImGui::GetDrawData();
	size_t vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
	size_t indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

	if ((vertexBufferSize == 0) || (indexBufferSize == 0)) {
		return;
	}

	auto vertexBufferHandle = renderGraph->NewGPUBufferHandle(EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, imDrawData->TotalVtxCount, sizeof(ImDrawVert));
	auto indexBufferHandle =  renderGraph->NewGPUBufferHandle(EBufferUsage::eIndexBuffer | EBufferUsage::eDataDst, imDrawData->TotalIdxCount, sizeof(ImDrawIdx));

	CVertexInputDescriptor vertexInputDesc{};
	vertexInputDesc.AddPrimitiveDescriptor(sizeof(ImDrawVert), {
		VertexAttribute{0, offsetof(ImDrawVert, pos), VertexInputFormat::eR32G32_SFloat}
		, VertexAttribute{1, offsetof(ImDrawVert, uv), VertexInputFormat::eR32G32_SFloat}
		, VertexAttribute{2, offsetof(ImDrawVert, col), VertexInputFormat::eR8G8B8A8_UNorm}
		});

	size_t vtxOffset = 0;
	size_t idxOffset = 0;
	std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> indexDataOffsets;
	std::vector<glm::uvec4> sissors;
	for (int n = 0; n < imDrawData->CmdListsCount; n++) {
		const ImDrawList* cmd_list = imDrawData->CmdLists[n];
		renderGraph->ScheduleBufferData(vertexBufferHandle, vtxOffset * sizeof(ImDrawVert), cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), cmd_list->VtxBuffer.Data);
		renderGraph->ScheduleBufferData(indexBufferHandle, idxOffset * sizeof(ImDrawIdx), cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), cmd_list->IdxBuffer.Data);
		
		for (int j = 0; j < cmd_list->CmdBuffer.Size; ++j)
		{
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
			indexDataOffsets.push_back(std::make_tuple(idxOffset, vtxOffset, pcmd->ElemCount));
			sissors.push_back(glm::ivec4(pcmd->ClipRect.x, pcmd->ClipRect.y, pcmd->ClipRect.z - pcmd->ClipRect.x, pcmd->ClipRect.w - pcmd->ClipRect.y));
			idxOffset += pcmd->ElemCount;
		}
		vtxOffset += cmd_list->VtxBuffer.Size;
	}

	CAttachmentInfo targetattachmentInfo{};
	targetattachmentInfo.format = renderGraph->GetTextureDescriptor(framebufferHandle).format;
	auto shaderConstants = renderGraph->NewShaderConstantSetHandle(imguiConstantBuilder);
	auto shaderBinding = renderGraph->NewShaderBindingSetHandle(imguiShaderBindingBuilder);
	shaderConstants.SetValue("IMGUIScale", meshScale);
	shaderBinding.SetConstantSet(imguiConstantBuilder.GetName(), shaderConstants)
		.SetSampler("FontSampler", sampler)
		.SetTexture("FontTexture", fontTexture);
	auto blendStates = ColorAttachmentsBlendStates::AlphaTransparent();
	renderGraph->NewRenderPass({ targetattachmentInfo })
		.SetAttachmentTarget(0, framebufferHandle)
		.Subpass({ {0} }
			, CPipelineStateObject{ {}, RasterizerStates::CullOff(), blendStates}
			, vertexInputDesc
			, shaderSet
			, { {}, {shaderBinding} }
			, [shaderBinding, vertexBufferHandle, indexBufferHandle, indexDataOffsets, sissors](CInlineCommandList& cmd)
			{
				cmd.SetShaderBindings(std::vector<ShaderBindingSetHandle>{ shaderBinding })
					.BindVertexBuffers({ vertexBufferHandle })
					.BindIndexBuffers(EIndexBufferType::e16, indexBufferHandle);
				for(uint32_t i = 0; i < indexDataOffsets.size(); ++i)
				{
					cmd.SetSissor(sissors[i].x, sissors[i].y, sissors[i].z, sissors[i].w)
						.DrawIndexed(std::get<2>(indexDataOffsets[i]), 1, std::get<0>(indexDataOffsets[i]), std::get<1>(indexDataOffsets[i]));
				}
			});
}

int main(int argc, char *argv[])
{
	std::filesystem::path rootPathFS{ "../../../../" , std::filesystem::path::format::native_format};
	std::filesystem::path rootPath = std::filesystem::absolute(rootPathFS);
	std::string resourceString = rootPath.string() + "CAResources";
	std::string assetString = rootPath.string() + "CAAssets";

	TModuleLoader<CThreadManager> threadManagerLoader("ThreadManager");
	TModuleLoader<CRenderBackend> renderBackendLoader("VulkanRenderBackend");
	TModuleLoader<IShaderCompiler> shaderCompilerLoader("ShaderCompiler");
	TModuleLoader<RenderInterfaceManager> renderInterfaceLoader("RenderInterface");
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

	ShaderConstantsBuilder imguiConstantBuilder{ "IMGUIConstants" };
	imguiConstantBuilder
		.Vec2<float>("IMGUIScale");

	ShaderBindingBuilder imguiBindingBuilder{ "IMGUIBinding" };
	imguiBindingBuilder.ConstantBuffer(imguiConstantBuilder);
	imguiBindingBuilder.Texture2D<float, 4>("FontTexture");
	imguiBindingBuilder.SamplerState("FontSampler");

	auto pThreadManager = threadManagerLoader.New();
	pThreadManager->InitializeThreadCount(5);

	auto pRenderInterface = renderInterfaceLoader.New();
	auto pShaderCompiler = shaderCompilerLoader.New();

	ShaderResourceLoader shaderResourceLoader;
	StaticMeshImporter staticMeshImporter;

	auto pResourceImportingSystem = resourceSystemFactory->NewImportingSystemShared();
	auto pResourceManagingSystem = resourceSystemFactory->NewManagingSystemShared();
	pResourceManagingSystem->SetResourceRootPath(assetString);
	pResourceImportingSystem->SetResourceManager(pResourceManagingSystem.get());
	pResourceImportingSystem->AddImporter(&shaderResourceLoader);
	pResourceImportingSystem->AddImporter(&staticMeshImporter);
	pResourceImportingSystem->ScanSourceDirectory(resourceString);

	ShaderResrouce* pTestShaderResource = nullptr;
	pResourceManagingSystem->LoadResource<ShaderResrouce>("Shaders/testShader.shaderbundle", [ppResource = &pTestShaderResource](ShaderResrouce* result)
		{
			*ppResource = result;
		});

	ShaderResrouce* pGeneralShaderResource = nullptr;
	pResourceManagingSystem->LoadResource<ShaderResrouce>("Shaders/generalVertexShader.shaderbundle", [ppResource = &pGeneralShaderResource](ShaderResrouce* result)
		{
			*ppResource = result;
		});

	ShaderResrouce* pImguiShaderResource = nullptr;
	pResourceManagingSystem->LoadResource<ShaderResrouce>("Shaders/imgui.shaderbundle", [ppResource = &pImguiShaderResource](ShaderResrouce* result)
		{
			*ppResource = result;
		});

	ShaderResrouce* pFinalBlitShaderResource = nullptr;
	pResourceManagingSystem->LoadResource<ShaderResrouce>("Shaders/finalBlitShader.shaderbundle", [ppResource = &pFinalBlitShaderResource](ShaderResrouce* result)
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


	GraphicsShaderSet shaderSet{};
	shaderSet.vert = &pTestShaderResource->m_VertexShaderProvider;
	shaderSet.frag = &pTestShaderResource->m_FragmentShaderProvider;

	GraphicsShaderSet finalBlitShaderSet{};
	finalBlitShaderSet.vert = &pFinalBlitShaderResource->m_VertexShaderProvider;
	finalBlitShaderSet.frag = &pFinalBlitShaderResource->m_FragmentShaderProvider;

	GraphicsShaderSet imguiShaderSet{};
	imguiShaderSet.vert = &pImguiShaderResource->m_VertexShaderProvider;
	imguiShaderSet.frag = &pImguiShaderResource->m_FragmentShaderProvider;

	auto pBackend = renderBackendLoader.New();
	pBackend->Initialize("Test Vulkan Backend", "CASCADED Engine");
	pBackend->InitializeThreadContextCount(5);
	pBackend->SetupGraphicsTaskGraph(pThreadManager->NewTaskGraph()
		->Name("Graphics Task Graph"));

	auto sampler = pBackend->GetOrCreateTextureSampler(TextureSamplerDescriptor{});

	auto texture0 = pBackend->CreateGPUTexture(GPUTextureDescriptor{
		pTextureResource0->GetWidth()
		, pTextureResource0->GetHeight()
		, pTextureResource0->GetFormat()
		, ETextureAccessType::eSampled | ETextureAccessType::eTransferDst
		});
	texture0->ScheduleTextureData(0, pTextureResource0->GetDataSize(), pTextureResource0->GetData());
	texture0->UploadAsync();

	auto texture1 = pBackend->CreateGPUTexture(GPUTextureDescriptor{
	pTextureResource1->GetWidth()
	, pTextureResource1->GetHeight()
	, pTextureResource1->GetFormat()
	, ETextureAccessType::eSampled | ETextureAccessType::eTransferDst
		});
	texture1->ScheduleTextureData(0, pTextureResource1->GetDataSize(), pTextureResource1->GetData());
	texture1->UploadAsync();

	auto samplingTextureBinding0 = pBackend->CreateShaderBindingSet(finalBlitBindingBuilder);
	samplingTextureBinding0->SetTexture("SourceTexture", texture0);
	samplingTextureBinding0->SetSampler("SourceSampler", sampler);

	auto samplingTextureBinding1 = pBackend->CreateShaderBindingSet(finalBlitBindingBuilder);
	samplingTextureBinding1->SetTexture("SourceTexture", texture1);
	samplingTextureBinding1->SetSampler("SourceSampler", sampler);

	auto windowHandle2 = pBackend->NewWindow(1024, 512, "Test Window2");

	{
		MeshGPUData newMeshGPUData{ pBackend };
		newMeshGPUData.UploadMeshResource(pTestMeshResource);
		g_MeshResourceToGPUData.insert(std::make_pair(pTestMeshResource, newMeshGPUData));
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


	std::vector<VertexData> vertexDataList = {
		VertexData{glm::vec2(-1.0f, -1.0f), glm::vec2(0.0f, 0.0f), glm::vec3(1, 1, 1)},
		VertexData{glm::vec2(1.0f, -1.0f), glm::vec2(1.0f, 0.0f), glm::vec3(1, 1, 1)},
		VertexData{glm::vec2(1.0f, 1.0f), glm::vec2(1.0f, 1.0f), glm::vec3(1, 1, 1)},
		VertexData{glm::vec2(-1.0f, 1.0f), glm::vec2(0.0f, 1.0f), glm::vec3(1, 1, 1)},
	};

	std::vector<uint16_t> indexDataList = {
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
	
	vertexBuffer->UploadAsync();
	indexBuffer->UploadAsync();
	

	std::chrono::high_resolution_clock timer;
	auto lastTime = timer.now();
	float deltaTime = 0.0f;
	Camera cam;
	bool mouseDown = false;
	glm::vec2 lastMousePos = {0.0f, 0.0f};
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	unsigned char* fontData;
	int texWidth, texHeight;
	io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);

	GPUTextureDescriptor fontDesc{};
	fontDesc.accessType = ETextureAccessType::eSampled | ETextureAccessType::eTransferDst;
	fontDesc.format = ETextureFormat::E_R8G8B8A8_UNORM;
	fontDesc.width = texWidth;
	fontDesc.height = texHeight;
	fontDesc.layers = 1;
	fontDesc.mipLevels = 1;
	fontDesc.textureType = ETextureType::e2D;
	auto fontImage = pBackend->CreateGPUTexture(fontDesc);
	fontImage->ScheduleTextureData(0, texWidth * texHeight * 4, fontData);
	fontImage->UploadAsync();

	std::shared_future<void> m_TaskFuture;
	m_TaskFuture = pBackend->GetGraphicsTaskGraph()->Run();
	m_TaskFuture.wait();
	while (pBackend->AnyWindowRunning())
	{
		uint64_t lastFrame = frame == 0 ? 0 : (frame - 1);

		auto baseTaskGraph = pThreadManager->NewTaskGraph()
			->Name("BaseTaskGraph")
			->WaitOnEvent("FullGraph", lastFrame)
			->SignalEvent("FullGraph", frame);
		auto gamePlayGraph  = baseTaskGraph->NewTaskGraph()
			->Name("GamePlayGraph")
			//->WaitOnEvent("GamePlay", lastFrame)
			//->SignalEvent("GamePlay", frame)
			;
		pBackend->SetupGraphicsTaskGraph(baseTaskGraph->NewTaskGraph()
			->Name("Graphics Task Graph")
			->DependsOn(gamePlayGraph)
			//->WaitOnEvent("Graphics", lastFrame)
			//->SignalEvent("Graphics", frame)
		);

		auto updateImGUIGraph = gamePlayGraph->NewTask()
			->Name("Update Imgui")
			->Functor([windowHandle2]()
			{
				auto& io = ImGui::GetIO();
				auto windowSize = windowHandle2->GetSizeSafe();
				UpdateIMGUI(windowSize.x, windowSize.y);
				io.MousePos = ImVec2(windowHandle2->GetMouseX(), windowHandle2->GetMouseY());
				io.MouseDown[0] = windowHandle2->IsMouseDown(CA_MOUSE_BUTTON_LEFT);
				io.MouseDown[1] = windowHandle2->IsMouseDown(CA_MOUSE_BUTTON_RIGHT);
				io.MouseDown[2] = windowHandle2->IsMouseDown(CA_MOUSE_BUTTON_MIDDLE);
			});

		auto updateCameraGraph = gamePlayGraph->NewTask()
			->Name("Update Camera")
			->Functor([plastMousePos = &lastMousePos
				 , pCam = &cam
				, pmouseDown = &mouseDown
				, pdrawInterface = &drawInterface
				, deltaTime
				, windowHandle2]()
				{
					int forwarding = 0;
					int lefting = 0;
					if (windowHandle2->IsKeyDown(CA_KEY_W))
					{
						++forwarding;
					}
					if (windowHandle2->IsKeyDown(CA_KEY_S))
					{
						--forwarding;
					}
					if (windowHandle2->IsKeyDown(CA_KEY_A))
					{
						++lefting;
					}
					if (windowHandle2->IsKeyDown(CA_KEY_D))
					{
						--lefting;
					}
					glm::vec2 mouseDelta = { 0.0f, 0.0f };
					glm::vec2 mousePos = { windowHandle2->GetMouseX(),windowHandle2->GetMouseY() };
					if (windowHandle2->IsMouseDown(CA_MOUSE_BUTTON_LEFT))
					{
						//std::cout << "Mouse Down!" << std::endl;
						if (!*pmouseDown)
						{
							*plastMousePos = mousePos;
							*pmouseDown = true;
						}
						mouseDelta = mousePos - *plastMousePos;
						*plastMousePos = mousePos;
					}
					else// if(windowHandle2->IsMouseUp(CA_MOUSE_BUTTON_LEFT))
					{
						*pmouseDown = false;
					}

					auto windowSize1 = windowHandle2->GetSizeSafe();
					pCam->Tick(deltaTime, forwarding, lefting, mouseDelta.x, mouseDelta.y, windowSize1.x, windowSize1.y);
					//std::cout << "Forward : " << forwarding << " Left : " << lefting << std::endl;
					//std::cout << "Mouse : " << mouseDelta.x << " " << mouseDelta.y << std::endl;

					pdrawInterface->Update(pCam->GetViewProjMatrix());
				});

		gamePlayGraph->NewTask()
			->Name("Setup Render Graph")
			->DependsOn(updateImGUIGraph)
			->DependsOn(updateCameraGraph)
			->Functor([pBackend
				, pRenderInterface
				, windowHandle2
				, pDrawInterface = &drawInterface
				, pVertexList = &vertexDataList
				, pIndexList = &indexDataList
				, pFinalBlitShaderSet  = &finalBlitShaderSet
				, pVertexInputDesc = &vertexInputDesc
				, pFinalBlitBindingDesc = &finalBlitBindingBuilder
				, sampler
				, fontImage
				, pimguiShaderSet = &imguiShaderSet
				, pimguiConstantBuilder = &imguiConstantBuilder
				, pimguiBindingBuilder = &imguiBindingBuilder]()
				{
					auto windowSize = windowHandle2->GetSizeSafe();

					auto pRenderGraph = pRenderInterface->NewRenderGraph();
					auto windowBackBuffer = pRenderGraph->RegisterWindowBackbuffer(windowHandle2.get());

					auto depthTextureHandle = pRenderGraph->NewTextureHandle(GPUTextureDescriptor{
						windowSize.x, windowSize.y
						, ETextureFormat::E_D32_SFLOAT
						, ETextureAccessType::eRT | ETextureAccessType::eSampled });

					auto colorTextureHandle = pRenderGraph->NewTextureHandle(GPUTextureDescriptor{
						windowSize.x, windowSize.y
						, ETextureFormat::E_R8G8B8A8_UNORM
						, ETextureAccessType::eRT | ETextureAccessType::eSampled });

					CAttachmentInfo targetattachmentInfo{};
					targetattachmentInfo.format = pRenderGraph->GetTextureDescriptor(windowBackBuffer).format;
					targetattachmentInfo.loadOp = EAttachmentLoadOp::eClear;
					targetattachmentInfo.clearValue = GraphicsClearValue::ClearColor(1.0f, 1.0f, 1.0f, 1.0f);

					CAttachmentInfo colorAttachmentInfo{};
					colorAttachmentInfo.format = ETextureFormat::E_R8G8B8A8_UNORM;
					colorAttachmentInfo.loadOp = EAttachmentLoadOp::eClear;
					colorAttachmentInfo.clearValue = GraphicsClearValue::ClearColor(0.0f, 1.0f, 1.0f, 1.0f);

					CAttachmentInfo depthAttachmentInfo{};
					depthAttachmentInfo.format = ETextureFormat::E_D32_SFLOAT;
					depthAttachmentInfo.loadOp = EAttachmentLoadOp::eClear;
					depthAttachmentInfo.clearValue = GraphicsClearValue::ClearDepthStencil(1.0f, 0x0);

					pRenderGraph->NewRenderPass({ colorAttachmentInfo, depthAttachmentInfo })
						.SetAttachmentTarget(0, colorTextureHandle)
						.SetAttachmentTarget(1, depthTextureHandle)
						.Subpass({ {0}, 1 }
							, {}
							, pDrawInterface
						);

					auto vertexBufferHandle = pRenderGraph->NewGPUBufferHandle(EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, pVertexList->size(), sizeof(VertexData))
						.ScheduleBufferData(0, pVertexList->size() * sizeof(VertexData), pVertexList->data());
					auto indexBufferHandle = pRenderGraph->NewGPUBufferHandle(EBufferUsage::eIndexBuffer | EBufferUsage::eDataDst, pIndexList->size(), sizeof(uint16_t))
						.ScheduleBufferData(0, pIndexList->size() * sizeof(uint16_t), pIndexList->data());

					ShaderBindingSetHandle blitBandingHandle = pRenderGraph->NewShaderBindingSetHandle(*pFinalBlitBindingDesc)
						.SetTexture("SourceTexture", colorTextureHandle)
						.SetSampler("SourceSampler", sampler);

					pRenderGraph->NewRenderPass({ targetattachmentInfo })
						.SetAttachmentTarget(0, windowBackBuffer)
						.Subpass({ {0} }
							, CPipelineStateObject{ {}, {RasterizerStates::CullOff()} }
							, *pVertexInputDesc
							, *pFinalBlitShaderSet
							, { {}, {blitBandingHandle} }
							, [blitBandingHandle, vertexBufferHandle, indexBufferHandle](CInlineCommandList& cmd)
							{
								cmd.SetShaderBindings(std::vector<ShaderBindingSetHandle>{ blitBandingHandle })
									.BindVertexBuffers({ vertexBufferHandle }, {})
									.BindIndexBuffers(EIndexBufferType::e16, indexBufferHandle)
									.DrawIndexed(6);
							});
					DrawIMGUI(pRenderGraph, windowBackBuffer, *pimguiShaderSet, *pimguiConstantBuilder, *pimguiBindingBuilder, sampler, fontImage);
					pBackend->ExecuteRenderGraph(pRenderGraph);
				});

		m_TaskFuture = baseTaskGraph->Run();
		//m_TaskFuture.wait();
		pBackend->TickWindows();
		pThreadManager->LogStatus();
		++frame;
		auto currentTime = timer.now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime).count();
		lastTime = currentTime;
		deltaTime = duration / 1000.0f;
		deltaTime = std::max(deltaTime, 0.005f);
		float frameRate = 1.0f / deltaTime;
	}
	if (m_TaskFuture.valid())
	{
		m_TaskFuture.wait();
	}
	pBackend->Release();
	pBackend.reset();
	pThreadManager.reset();
	ImGui::DestroyContext();
	return EXIT_SUCCESS;
}
