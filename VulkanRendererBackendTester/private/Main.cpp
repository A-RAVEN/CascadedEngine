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
	//shaderSet.vert = &pTestShaderResource->m_VertexShaderProvi
	// der;
	//shaderSet.frag = &pTestShaderResource->m_FragmentShaderProvider;

	//GraphicsShaderSet finalBlitShaderSet{};
	//finalBlitShaderSet.vert = &pFinalBlitShaderResource->m_VertexShaderProvider;
	//finalBlitShaderSet.frag = &pFinalBlitShaderResource->m_FragmentShaderProvider;



	auto pBackend = renderBackendLoader.New();
	pBackend->Initialize("Test Vulkan Backend", "CASCADED Engine");
	pBackend->InitializeThreadContextCount(5);

	auto windowHandle = pBackend->NewWindow(1024, 512, "Test Window2", true, false, true, false);
	IMGUIContext imguiContext{};
	imguiContext.Initialize(pBackend.get(), windowHandle.get(), pResourceManagingSystem.get());

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


	{
		MeshGPUData newMeshGPUData{ pBackend };
		newMeshGPUData.UploadMeshResource(pTestMeshResource, "VikingScene");
		g_MeshResourceToGPUData.insert(castl::make_pair(pTestMeshResource, newMeshGPUData));
	}

	//MeshBatchDrawInterface drawInterface{};
	//{
	//	MeshRenderer meshRenderer{};
	//	meshRenderer.p_MeshResource = pTestMeshResource;
	//	meshRenderer.materials = {
	//		MeshMaterial 
	//		{
	//			CPipelineStateObject{ DepthStencilStates::NormalOpaque() }
	//			, { &pGeneralShaderResource->m_VertexShaderProvider, &pGeneralShaderResource->m_FragmentShaderProvider }
	//			, { samplingTextureBinding1 }
	//		},
	//		MeshMaterial 
	//		{
	//			CPipelineStateObject{ DepthStencilStates::NormalOpaque() }
	//			, { &pGeneralShaderResource->m_VertexShaderProvider, &pGeneralShaderResource->m_FragmentShaderProvider }
	//			, { samplingTextureBinding0 }
	//		}
	//	};
	//	drawInterface.AddMesh(meshRenderer, glm::mat4(1.0f));

	//	drawInterface.MakeBatch(pBackend.get());
	//}


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

	//CVertexInputDescriptor vertexInputDesc{};
	//vertexInputDesc.AddPrimitiveDescriptor(sizeof(VertexData), {
	//	VertexAttribute{0, offsetof(VertexData, pos), VertexInputFormat::eR32G32_SFloat}
	//	, VertexAttribute{1, offsetof(VertexData, uv), VertexInputFormat::eR32G32_SFloat}
	//	, VertexAttribute{2, offsetof(VertexData, color), VertexInputFormat::eR32G32B32_SFloat}
	//	});


	ShaderBindingDescriptorList bindingSetList = { shaderBindingBuilder };

	VertexInputsDescriptor vertexInputDesc = VertexInputsDescriptor::Create(
		sizeof(VertexData),
		{
			VertexAttribute::Create("POSITION", offsetof(VertexData, pos), VertexInputFormat::eR32G32_SFloat),
			VertexAttribute::Create("TEXCOORD0", offsetof(VertexData, uv), VertexInputFormat::eR32G32_SFloat),
			VertexAttribute::Create("COLOR", offsetof(VertexData, color), VertexInputFormat::eR32G32B32_SFloat),
		}
	);




	
	castl::chrono::high_resolution_clock timer;
	auto lastTime = timer.now();
	float deltaTime = 0.0f;
	Camera cam;
	bool mouseDown = false;
	glm::vec2 lastMousePos = {0.0f, 0.0f};


	pThreadManager->SetupFunction([
				pBackend
				, windowHandle
				, pRenderInterface
				, pFinalBlitShaderResource
				, &vertexInputDesc
		](auto setup)
		{
			castl::shared_ptr<ShaderArgList> shaderArgList = castl::make_shared<ShaderArgList>();
			shaderArgList->SetValue("TestColor", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
			castl::shared_ptr<GPUGraph> newGraph = castl::make_shared<GPUGraph>();
			BufferHandle vertexBufferHandle{ "QuadVertexBuffer" };
			BufferHandle indexBufferHandle{ "QuadIndexBuffer" };
			newGraph->AllocBuffer(vertexBufferHandle, GPUBufferDescriptor::Create(EBufferUsage::eVertexBuffer, 4, sizeof(VertexData)))
				.AllocBuffer(indexBufferHandle, GPUBufferDescriptor::Create(EBufferUsage::eIndexBuffer, 6, sizeof(uint16_t)))
				.NewRenderPass(windowHandle)
				.DefineVertexInputBinding("QuadDesc", vertexInputDesc)
				.SetPipelineState({})
				.SetShaderArguments(shaderArgList)
				.SetShaders(pFinalBlitShaderResource)
				.DrawCall()
				.SetVertexBuffer("QuadDesc", vertexBufferHandle)
				.SetIndexBuffer(EIndexBufferType::e16, indexBufferHandle, 0)
				.Draw([](CommandList& commandList)
					{
						commandList.DrawIndexed(6);
					});

			GPUFrame gpuFrame{};
			gpuFrame.graphs.push_back(newGraph);
			gpuFrame.presentWindows.push_back(windowHandle);
			pBackend->ScheduleGPUFrame(setup, gpuFrame);

			return true;
		}, "FullGraph");
	pThreadManager->RunSetupFunction();
	pThreadManager->LogStatus();
	imguiContext.Release();
	pBackend->Release();
	pBackend.reset();
	pThreadManager.reset();
	return EXIT_SUCCESS;
}
