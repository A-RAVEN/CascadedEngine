#include <cstdlib>
#define NOMINMAX
#include <windows.h>
#include <RenderInterface/header/CRenderBackend.h>
#include <ThreadManager/header/ThreadManager.h>
#include <ShaderCompiler/header/Compiler.h>
#include <iostream>
#include <SharedTools/header/library_loader.h>
#include <RenderInterface/header/RenderInterfaceManager.h>
#include <RenderInterface/header/CNativeRenderPassInfo.h>
#include <RenderInterface/header/CCommandList.h>
#include <SharedTools/header/FileLoader.h>
#include "TestShaderProvider.h"
#include <RenderInterface/header/ShaderBindingBuilder.h>
#include <ExternalLib/glm/glm/mat4x4.hpp>
#include <ExternalLib/glm/glm/gtc/matrix_transform.hpp>
#include <ExternalLib/stb/stb_image.h>
#include <chrono>
using namespace thread_management;
using namespace library_loader;
using namespace graphics_backend;
using namespace ShaderCompiler;
using namespace uenum;

struct VertexData
{
	glm::vec2 pos;
	glm::vec2 uv;
	glm::vec3 color;
};

class TestMeshInterface : public IMeshInterface
{
public:
	// 通过 IMeshInterface 继承
	size_t GetGraphicsPipelineStatesCount() override
	{
		return 1;
	}
	GraphicsPipelineStatesData& GetGraphicsPipelineStatesData(size_t index) override
	{
		return m_Data;
	}
	size_t GetBatchCount() override
	{
		return 1;
	}

	void Update()
	{
		auto lookat = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		auto perspective = glm::perspective(glm::radians(45.0f), 1024.0f / 512.0f, 0.1f, 1000.0f);
		glm::mat4 data = perspective * lookat;
		pShaderConstantSet->SetValue("viewProjectionMatrix", data);
	}

	void DrawBatch(size_t batchIndex, CInlineCommandList& commandList) override
	{
		commandList.BindPipelineState(0);
		if (pVertexBuffer->UploadingDone() && pIndexBuffer->UploadingDone())
		{
			commandList.SetShaderBindings({ pShaderBindingSet });
			commandList.BindVertexBuffers({ pVertexBuffer.get() }, {});
			commandList.BindIndexBuffers(EIndexBufferType::e16, pIndexBuffer.get());
			commandList.DrawIndexed(6);
		}
		else
		{
			std::cout << "Not Finish Yet" << std::endl;
		}
	}

	void Initialize(std::shared_ptr<CRenderBackend> pBackend
		, ShaderBindingBuilder const& bindingBuilder
		, ShaderConstantsBuilder const& shaderConstantBuilder
		, std::shared_ptr<GPUTexture> pTexture
		, std::shared_ptr<GPUBuffer> vertexBuffer
		, std::shared_ptr<GPUBuffer> indexBuffer
		, GraphicsPipelineStatesData const& data)
	{
		m_Data = data;
		pShaderConstantSet = pBackend->CreateShaderConstantSet(shaderConstantBuilder);
		pShaderBindingSet = pBackend->CreateShaderBindingSet(bindingBuilder);
		pVertexBuffer = vertexBuffer;
		pIndexBuffer = indexBuffer;

		auto sampler = pBackend->GetOrCreateTextureSampler(TextureSamplerDescriptor{});
		pShaderBindingSet->SetConstantSet(pShaderConstantSet->GetName(), pShaderConstantSet);
		pShaderBindingSet->SetTexture("TestTexture", pTexture);
		pShaderBindingSet->SetSampler("TestSampler", sampler);
	}
private:
	GraphicsPipelineStatesData m_Data;
	std::shared_ptr<ShaderConstantSet> pShaderConstantSet;
	std::shared_ptr<ShaderBindingSet> pShaderBindingSet;
	std::shared_ptr<GPUBuffer> pVertexBuffer;
	std::shared_ptr<GPUBuffer> pIndexBuffer;
};

int main(int argc, char *argv[])
{
	TModuleLoader<CThreadManager> threadManagerLoader("ThreadManager");
	TModuleLoader<CRenderBackend> renderBackendLoader("VulkanRenderBackend");
	TModuleLoader<IShaderCompiler> shaderCompilerLoader("ShaderCompiler");
	TModuleLoader<RenderInterfaceManager> renderInterfaceLoader("RenderInterface");

	ShaderConstantsBuilder shaderConstantBuilder{ "DefaultCameraConstants" };
	shaderConstantBuilder
		.Mat4<float>("viewProjectionMatrix");

	ShaderBindingBuilder shaderBindingBuilder{ "TestBinding" };
	shaderBindingBuilder.ConstantBuffer(shaderConstantBuilder);
	shaderBindingBuilder.Texture2D<float, 1>("TestTexture");
	shaderBindingBuilder.SamplerState("TestSampler");

	auto pThreadManager = threadManagerLoader.New();
	pThreadManager->InitializeThreadCount(5);

	auto pRenderInterface = renderInterfaceLoader.New();


	auto pShaderCompiler = shaderCompilerLoader.New();

	auto shaderSource = fileloading_utils::LoadStringFile("D:/Projects/Crimson/Build/x64/Debug/testShader.hlsl");

	auto spirVResult = pShaderCompiler->CompileShaderSource(EShaderSourceType::eHLSL
		, "testShader.hlsl"
		, shaderSource
		, "vert"
		, ECompileShaderType::eVert
		, false, true);


	std::shared_ptr<TestShaderProvider> vertProvider = std::make_shared<TestShaderProvider>();
	vertProvider->SetUniqueName("testShader.hlsl.vert");
	vertProvider->SetData("spirv", "vert", spirVResult.data(), spirVResult.size() * sizeof(uint32_t));


	spirVResult = pShaderCompiler->CompileShaderSource(EShaderSourceType::eHLSL
		, "testShader.hlsl"
		, shaderSource
		, "frag"
		, ECompileShaderType::eFrag
		, false, true);

	std::shared_ptr<TestShaderProvider> fragProvider = std::make_shared<TestShaderProvider>();
	fragProvider->SetUniqueName("testShader.hlsl.frag");
	fragProvider->SetData("spirv", "frag", spirVResult.data(), spirVResult.size() * sizeof(uint32_t));

	GraphicsShaderSet shaderSet{};
	shaderSet.vert = vertProvider;
	shaderSet.frag = fragProvider;


	auto pBackend = renderBackendLoader.New();
	pBackend->Initialize("Test Vulkan Backend", "CASCADED Engine");
	pBackend->InitializeThreadContextCount(pThreadManager.get(), 5);
	auto windowHandle = pBackend->NewWindow(1024, 512, "Test Window");

	auto shaderConstants = pBackend->CreateShaderConstantSet(shaderConstantBuilder);

	auto shaderBindings = pBackend->CreateShaderBindingSet(shaderBindingBuilder);
	shaderBindings->SetConstantSet(shaderConstants->GetName(), shaderConstants);

	std::vector<VertexData> vertexDataList = {
		VertexData{glm::vec2(-0.5f, -0.5f), glm::vec2(0.0f, 0.0f), glm::vec3(1, 1, 1)},
		VertexData{glm::vec2(0.5f, -0.5f), glm::vec2(1.0f, 0.0f), glm::vec3(1, 1, 1)},
		VertexData{glm::vec2(0.5f, 0.5f), glm::vec2(1.0f, 1.0f), glm::vec3(1, 1, 1)},
		VertexData{glm::vec2(-0.5f, 0.5f), glm::vec2(0.0f, 1.0f), glm::vec3(1, 1, 1)},
	};

	std::vector<uint16_t> indexDataList = {
		0, 1, 2, 2, 3, 0
	};

	int w, h, channel_num;
	auto data = stbi_load("D:/Projects/Crimson/Build/x64/Debug/MonValley_A_LookoutPoint_2k.hdr", &w, &h, &channel_num, 4);
	size_t imgSize = w * h * sizeof(uint32_t);

	GPUTextureDescriptor desc{};
	desc.accessType = ETextureAccessType::eSampled | ETextureAccessType::eTransferDst;
	desc.format = ETextureFormat::E_R8G8B8A8_UNORM;
	desc.width = w;
	desc.height = h;
	desc.layers = 1;
	desc.mipLevels = 1;
	desc.textureType = ETextureType::e2D;
	auto image = pBackend->CreateGPUTexture(desc);

	auto sampler = pBackend->GetOrCreateTextureSampler(TextureSamplerDescriptor{});

	shaderBindings->SetTexture("TestTexture", image);
	shaderBindings->SetSampler("TestSampler", sampler);


	image->ScheduleTextureData(0, imgSize, data);

	stbi_image_free(data);

	auto vertexBuffer = pBackend->CreateGPUBuffer(
		EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, vertexDataList.size(), sizeof(VertexData));
	vertexBuffer->ScheduleBufferData(0, vertexDataList.size() * sizeof(VertexData), vertexDataList.data());


	auto indexBuffer = pBackend->CreateGPUBuffer(
		EBufferUsage::eIndexBuffer | EBufferUsage::eDataDst, indexDataList.size(), sizeof(uint16_t));
	indexBuffer->ScheduleBufferData(0, indexDataList.size() * sizeof(uint16_t), indexDataList.data());

	int32_t frame = 0;

	CVertexInputDescriptor vertexInputDesc{};
	vertexInputDesc.AddPrimitiveDescriptor(sizeof(VertexData), {
		VertexAttribute{0, offsetof(VertexData, pos), VertexInputFormat::eR32G32_SFloat}
		, VertexAttribute{1, offsetof(VertexData, uv), VertexInputFormat::eR32G32_SFloat}
		, VertexAttribute{2, offsetof(VertexData, color), VertexInputFormat::eR32G32B32_SFloat}
		});

	ShaderBindingDescriptorList bindingSetList = { shaderBindingBuilder };

	
	vertexBuffer->UploadAsync();
	indexBuffer->UploadAsync();
	image->UploadAsync();
	

	GraphicsPipelineStatesData stateData{};
	stateData.pipelineStateObject = CPipelineStateObject{};
	stateData.shaderBindingDescriptors = { shaderBindingBuilder };
	stateData.shaderSet = shaderSet;
	stateData.vertexInputDescriptor = vertexInputDesc;

	TestMeshInterface meshBatch{};
	meshBatch.Initialize(pBackend, shaderBindingBuilder, shaderConstantBuilder, image, vertexBuffer, indexBuffer, stateData);

	std::chrono::high_resolution_clock timer;
	auto lastTime = timer.now();
	float deltaTime = 0.0f;
	float rotation = 0.0f;
	while (pBackend->AnyWindowRunning())
	{
		auto windowSize = windowHandle->GetSizeSafe();

		rotation += deltaTime * glm::radians(50.0f);
		auto rotMat = glm::rotate(glm::mat4(1), rotation, glm::vec3(0.0f, 0.0f, 1.0f));
		auto translation = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		auto lookat = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		auto perspective = glm::perspective(glm::radians(45.0f), static_cast<float>(windowSize.x) / static_cast<float>(windowSize.y), 0.1f, 1000.0f);
		glm::mat4 data = perspective * lookat * translation * rotMat;
		shaderConstants->SetValue("viewProjectionMatrix", data);

		meshBatch.Update();

		auto pRenderGraph = pRenderInterface->NewRenderGraph();
		auto windowBackBuffer = pRenderGraph->RegisterWindowBackbuffer(windowHandle);
		auto depthTextureHandle = pRenderGraph->NewTextureHandle(GPUTextureDescriptor{ 1024, 512, ETextureFormat::E_D32_SFLOAT, ETextureAccessType::eRT | ETextureAccessType::eSampled});
		
		CAttachmentInfo attachmentInfo{};
		attachmentInfo.format = windowBackBuffer.GetDescriptor().format;
		attachmentInfo.loadOp = EAttachmentLoadOp::eClear;
		attachmentInfo.clearValue = GraphicsClearValue::ClearColor(0.0f, 1.0f, 1.0f, 1.0f);

		CAttachmentInfo depthAttachmentInfo{};
		depthAttachmentInfo.format = ETextureFormat::E_D32_SFLOAT;
		depthAttachmentInfo.loadOp = EAttachmentLoadOp::eClear;
		depthAttachmentInfo.clearValue = GraphicsClearValue::ClearDepthStencil(0.0f, 0x0);

		CRenderpassBuilder& newRenderPass = pRenderGraph->NewRenderPass({ attachmentInfo });
		newRenderPass.SetAttachmentTarget(0, windowBackBuffer);
		//newRenderPass.SetAttachmentTarget(1, depthTextureHandle);
		newRenderPass
			.Subpass({ {0} }
				, &meshBatch
			)
			.Subpass({ {0} }
				, CPipelineStateObject{}
				, vertexInputDesc
				, shaderSet
				, { {shaderBindings}, {} }
				, [vertexBuffer, indexBuffer, shaderBindings](CInlineCommandList& cmd)
				{
					if (vertexBuffer->UploadingDone() && indexBuffer->UploadingDone())
					{
						cmd.SetShaderBindings({ shaderBindings });
						cmd.BindVertexBuffers({ vertexBuffer.get() }, {});
						cmd.BindIndexBuffers(EIndexBufferType::e16, indexBuffer.get());
						cmd.DrawIndexed(6);
					}
					else
					{
						std::cout << "Not Finish Yet" << std::endl;
					}
				});

				pRenderGraph->PresentWindow(windowHandle);

		pBackend->ExecuteRenderGraph(pRenderGraph);
		pBackend->TickBackend();
		pBackend->TickWindows();
		++frame;
		auto currentTime = timer.now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime).count();
		lastTime = currentTime;
		deltaTime = duration / 1000.0f;
		deltaTime = std::max(deltaTime, 0.005f);
		float frameRate = 1.0f / deltaTime;
		//std::cout << frameRate << std::endl;
	}
	pBackend->Release();
	pBackend.reset();
	pThreadManager.reset();

	return EXIT_SUCCESS;
}
