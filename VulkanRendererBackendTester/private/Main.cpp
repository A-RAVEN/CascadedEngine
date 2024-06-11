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
using namespace thread_management;
using namespace library_loader;
using namespace graphics_backend;
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

	auto pThreadManager = threadManagerLoader.New();
	pThreadManager->InitializeThreadCount(5);

	auto pRenderInterface = renderInterfaceLoader.New();

	ShaderResourceLoaderSlang slangShaderResourceLoader;
	StaticMeshImporter staticMeshImporter;

	auto pResourceManagingSystem = resourceSystemFactory->NewManagingSystemShared();
	pResourceManagingSystem->SetResourceRootPath(assetString);

	auto pResourceImportingSystem = resourceSystemFactory->NewImportingSystemShared();
	pResourceImportingSystem->SetResourceManager(pResourceManagingSystem.get());
	pResourceImportingSystem->AddImporter(&slangShaderResourceLoader);
	pResourceImportingSystem->AddImporter(&staticMeshImporter);
	pResourceImportingSystem->ScanSourceDirectory(resourceString);

	//ShaderResrouce* pGeneralShaderResource = nullptr;
	//pResourceManagingSystem->LoadResource<ShaderResrouce>("Shaders/TestStaticMeshShader.shaderbundle", [ppResource = &pGeneralShaderResource](ShaderResrouce* result)
	//	{
	//		*ppResource = result;
	//	});

	ShaderResrouce* pFinalBlitShaderResource = nullptr;
	pResourceManagingSystem->LoadResource<ShaderResrouce>("Shaders/testFinalBlit.shaderbundle", [ppResource = &pFinalBlitShaderResource](ShaderResrouce* result)
		{
			*ppResource = result;
		});

	ShaderResrouce* pTestComputeShaderResource = nullptr;
	pResourceManagingSystem->LoadResource<ShaderResrouce>("Shaders/TestComputeShader.shaderbundle", [ppResource = &pTestComputeShaderResource](ShaderResrouce* result)
		{
			*ppResource = result;
		});

	ShaderResrouce* pFinalBlitShader = nullptr;
	pResourceManagingSystem->LoadResource<ShaderResrouce>("Shaders/FinalBlit.shaderbundle", [&pFinalBlitShader](ShaderResrouce* result)
		{
			pFinalBlitShader = result;
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

	auto pBackend = renderBackendLoader.New();
	pBackend->Initialize("Test Vulkan Backend", "CASCADED Engine");
	pBackend->InitializeThreadContextCount(5);

	auto windowHandle = pBackend->NewWindow(1024, 512, "Test Window2", true, false, true, false);

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

	pThreadManager->OneTime([
		vertexBuffer,
		indexBuffer,
		pTextureResource0,
		&vertexDataList,
		&indexDataList,
		&texture,
		pBackend
	](auto setup)
	{
		castl::shared_ptr<GPUGraph> submitGraph = castl::make_shared<GPUGraph>();
		submitGraph->ScheduleData(BufferHandle{ vertexBuffer }, vertexDataList.data(), vertexDataList.size() * sizeof(vertexDataList[0]));
		submitGraph->ScheduleData(BufferHandle{ indexBuffer }, indexDataList.data(), indexDataList.size() * sizeof(indexDataList[0]));
		submitGraph->ScheduleData(texture, pTextureResource0->GetData(), pTextureResource0->GetDataSize());
		GPUFrame submitFrame{};
		submitFrame.pGraph = submitGraph;
		pBackend->ScheduleGPUFrame(setup, submitFrame);
	}, "");

	VertexInputsDescriptor vertexInputDesc = VertexInputsDescriptor::Create(
		sizeof(VertexData),
		{
			VertexAttribute::Create(offsetof(VertexData, pos), VertexInputFormat::eR32G32_SFloat, "POSITION"),
			VertexAttribute::Create(offsetof(VertexData, uv), VertexInputFormat::eR32G32_SFloat, "TEXCOORD", 0),
			VertexAttribute::Create(offsetof(VertexData, color), VertexInputFormat::eR32G32B32_SFloat, "COLOR"),
		}
	);

	castl::shared_ptr<ShaderArgList> textureArgList = castl::make_shared<ShaderArgList>();
	textureArgList->SetImage("SourceTexture", texture);
	textureArgList->SetSampler("SourceSampler", TextureSamplerDescriptor::Create());

	pThreadManager->LoopFunction([
				pBackend
				, windowHandle
				, pRenderInterface
				, pFinalBlitShaderResource
				, pFinalBlitShader
				, pTestComputeShaderResource
				, &vertexBuffer
				, &indexBuffer
				, &vertexInputDesc
				, &textureArgList
				, texture
				, pTextureResource1
	](auto setup)
	{
		//setup->ForceRunOnMainThread();
		castl::shared_ptr<ShaderArgList> shaderArgList = castl::make_shared<ShaderArgList>();
		shaderArgList->SetValue("TestColor", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
		shaderArgList->SetSubArgList("textures", textureArgList);

		BufferHandle tempBufferHandle{"TempBuffer" };
		castl::shared_ptr<ShaderArgList> computeShaderArgList = castl::make_shared<ShaderArgList>();
		computeShaderArgList->SetBuffer("RWBuffer", tempBufferHandle);

		castl::shared_ptr<GPUGraph> newGraph = castl::make_shared<GPUGraph>();

		ImageHandle tempTextureHandle{"TempTexture" };
		shaderArgList->SetImage("SourceTexture1", tempTextureHandle, GPUTextureView::CreateDefaultForSampling(pTextureResource1->GetFormat()));
		
		auto drawCall = DrawCallBatch::New()
			.SetVertexBuffer(vertexInputDesc, vertexBuffer)
			.SetIndexBuffer(EIndexBufferType::e16, indexBuffer, 0)
			.Draw([](CommandList& commandList)
				{
					commandList.DrawIndexed(6);
				});
		
		ImageHandle tmpTarget{"TmpTarget" };
		castl::shared_ptr<ShaderArgList> finalBlitShaderArgList = castl::make_shared<ShaderArgList>();
		finalBlitShaderArgList->SetImage("SourceTexture", tmpTarget, GPUTextureView::CreateDefaultForSampling(windowHandle->GetBackbufferDescriptor().format));
		finalBlitShaderArgList->SetSampler("SourceSampler", TextureSamplerDescriptor::Create());

		auto tmpDesc = windowHandle->GetBackbufferDescriptor();
		tmpDesc.accessType = ETextureAccessType::eRT | ETextureAccessType::eSampled;
		newGraph->
			AllocImage(tempTextureHandle, GPUTextureDescriptor::Create(pTextureResource1->GetWidth(), pTextureResource1->GetHeight(), pTextureResource1->GetFormat(), ETextureAccessType::eSampled | ETextureAccessType::eTransferDst))
			.ScheduleData(tempTextureHandle, pTextureResource1->GetData(), pTextureResource1->GetDataSize())
			.AllocBuffer(tempBufferHandle, GPUBufferDescriptor::Create(EBufferUsage::eStructuredBuffer | EBufferUsage::eDataSrc, 1, sizeof(glm::vec4)))
			.AddPass(ComputeBatch::New()
				.PushArgList(computeShaderArgList)
				.Dispatch(pTestComputeShaderResource, "testGenerator0", 1, 1, 1))
			.AllocImage(tmpTarget, tmpDesc)
			.AddPass(RenderPass::New(tmpTarget)
				.SetPipelineState({})
				.SetShaderArguments(shaderArgList)
				.SetShaders(pFinalBlitShaderResource)
				.DrawCall(drawCall))
			.AddPass
			(
				RenderPass::New(windowHandle)
				.SetPipelineState({})
				.SetShaderArguments(finalBlitShaderArgList)
				.SetShaders(pFinalBlitShader)
				.DrawCall
				(
					DrawCallBatch::New()
					.SetVertexBuffer(vertexInputDesc, vertexBuffer)
					.SetIndexBuffer(EIndexBufferType::e16, indexBuffer, 0)
					.Draw([](CommandList& commandList)
						{
							commandList.DrawIndexed(6);
						})
				)
			);

		GPUFrame gpuFrame{};
		gpuFrame.pGraph = newGraph;
		gpuFrame.presentWindows.push_back(windowHandle);
		pBackend->ScheduleGPUFrame(setup, gpuFrame);
		return true;
	}, "FullGraph");
	pThreadManager->Run();
	pThreadManager->LogStatus();
	pThreadManager.reset();
	pBackend->Release();
	pBackend.reset();
	return EXIT_SUCCESS;
}
