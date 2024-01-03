#include <cstdlib>
#define NOMINMAX
#include <windows.h>
#include <ThreadManager/header/ThreadManager.h>
#include <ShaderCompiler/header/Compiler.h>
#include <iostream>
#include <SharedTools/header/library_loader.h>
#include <RenderInterface/header/CRenderBackend.h>
#include <RenderInterface/header/RenderInterfaceManager.h>
#include <RenderInterface/header/CNativeRenderPassInfo.h>
#include <RenderInterface/header/CCommandList.h>
#include <RenderInterface/header/ShaderBindingBuilder.h>
#include <SharedTools/header/FileLoader.h>
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

class MeshInfo
{
public:
	CVertexInputDescriptor vertexInputDescriptor;
	std::shared_ptr<GPUBuffer> vertexBuffer;
	std::shared_ptr<GPUBuffer> indexBuffer;
	EIndexBufferType bufferType;
	uint32_t indexCount;

	MeshInfo(CVertexInputDescriptor const& vertexInputDescriptor
		, std::shared_ptr<GPUBuffer> vertexBuffer
		, std::shared_ptr<GPUBuffer> indexBuffer
		, EIndexBufferType bufferType
		, uint32_t indexCount)
		: vertexInputDescriptor(vertexInputDescriptor)
		, vertexBuffer(vertexBuffer)
		, indexBuffer(indexBuffer)
		, bufferType(bufferType)
		, indexCount(indexCount)
	{
	}
};

class TestMeshInterface : public IMeshInterface
{
public:
	// 通过 IMeshInterface 继承
	size_t GetGraphicsPipelineStatesCount() override
	{
		return m_Datas.size();
	}
	GraphicsPipelineStatesData& GetGraphicsPipelineStatesData(size_t index) override
	{
		return m_Datas[index];
	}
	size_t GetBatchCount() override
	{
		return m_Datas.size();
	}

	void Update(glm::mat4 const& viewProjMatrix)
	{
		m_PerViewShaderConstants->SetValue("ViewProjectionMatrix", viewProjMatrix);
	}

	void DrawBatch(size_t batchIndex, CInlineCommandList& commandList) override
	{
		commandList.BindPipelineState(batchIndex);
		if (m_VertexBuffers[batchIndex]->UploadingDone() && m_IndexBuffers[batchIndex]->UploadingDone())
		{
			commandList.SetShaderBindings({ m_PerViewShaderBindings, m_ShaderBindingSets[batchIndex]});
			commandList.BindVertexBuffers({ m_VertexBuffers[batchIndex].get() }, {});
			commandList.BindIndexBuffers(EIndexBufferType::e16, m_IndexBuffers[batchIndex].get());
			commandList.DrawIndexed(6);
		}
		else
		{
			std::cout << "Not Finish Yet" << std::endl;
		}
	}

	void Initialize(std::shared_ptr<CRenderBackend> pBackend)
	{
		ShaderConstantsBuilder perViewConstantsBuilder{ "PerViewConstants" };
		perViewConstantsBuilder.Mat4<float>("ViewProjectionMatrix");
		m_PerViewShaderConstants = pBackend->CreateShaderConstantSet(perViewConstantsBuilder);
		ShaderBindingBuilder perViewBindingBuilder("PerViewBindings");
		perViewBindingBuilder.ConstantBuffer(perViewConstantsBuilder);
		m_PerViewShaderBindings = pBackend->CreateShaderBindingSet(perViewBindingBuilder);
		m_PerViewShaderBindings->SetConstantSet(m_PerViewShaderConstants->GetName()
			, m_PerViewShaderConstants);
	}

	void InsertMeshDrawcall(
		MeshInfo const& meshInfo
		, CPipelineStateObject const& pipelineStates
		, GraphicsShaderSet const& shaderSet
		, std::shared_ptr<ShaderBindingSet> meshBoundShaderBindings
	)
	{
		GraphicsPipelineStatesData data{};
		data.pipelineStateObject = pipelineStates;
		data.shaderSet = shaderSet;
		data.shaderBindingDescriptors = {
			m_PerViewShaderBindings->GetBindingSetDesc()
		, meshBoundShaderBindings->GetBindingSetDesc() };
		data.vertexInputDescriptor = meshInfo.vertexInputDescriptor;
		m_Datas.push_back(data);
		m_ShaderBindingSets.push_back(meshBoundShaderBindings);
		m_VertexBuffers.push_back(meshInfo.vertexBuffer);
		m_IndexBuffers.push_back(meshInfo.indexBuffer);
	}
private:
	std::shared_ptr<ShaderConstantSet> m_PerViewShaderConstants;
	std::shared_ptr<ShaderBindingSet> m_PerViewShaderBindings;
	std::vector<GraphicsPipelineStatesData> m_Datas;
	std::vector<std::shared_ptr<ShaderBindingSet>> m_ShaderBindingSets;
	std::vector<std::shared_ptr<GPUBuffer>> m_VertexBuffers;
	std::vector<std::shared_ptr<GPUBuffer>> m_IndexBuffers;
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
			, CPipelineStateObject{ {}, {}, blendStates }
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
	pResourceManagingSystem->LoadResource<StaticMeshResource>("Models/VikingRoom.mesh", [ppResource = &pTestMeshResource](StaticMeshResource* result)
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
	pBackend->InitializeThreadContextCount(pThreadManager.get(), 5);



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
		meshRenderer.pipelineStateObject = CPipelineStateObject{ DepthStencilStates::NormalOpaque() };
		meshRenderer.pipelineStateObject.rasterizationStates.frontFace = EFrontFace::eCounterClockWise;
		meshRenderer.shaderBindingDescriptors = {};
		meshRenderer.shaderSet = { &pGeneralShaderResource->m_VertexShaderProvider, &pGeneralShaderResource->m_FragmentShaderProvider };
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
		0, 1, 2, 2, 3, 0
	};

	int w, h, channel_num;
	auto data = stbi_load((resourceString + "/Images/MonValley_A_LookoutPoint_2k.hdr").c_str(), &w, &h, &channel_num, 4);
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

	MeshInfo meshInfo{ vertexInputDesc
		, vertexBuffer
		, indexBuffer
		, EIndexBufferType::e16
		, 6};

	std::vector<std::shared_ptr<ShaderBindingSet>> meshShaderBindings;
	std::vector<std::shared_ptr<ShaderConstantSet>> meshShaderConstants;

	meshShaderBindings.resize(16);
	meshShaderConstants.resize(16);

	TestMeshInterface meshBatch{};
	meshBatch.Initialize(pBackend);

	for (uint16_t xx = 0; xx < 4; ++xx)
	{
		for (uint16_t yy = 0; yy < 4; ++yy)
		{
			uint32_t index = xx * 4 + yy;
			meshShaderConstants[index] = pBackend->CreateShaderConstantSet(shaderConstantBuilder);
			meshShaderBindings[index] = pBackend->CreateShaderBindingSet(shaderBindingBuilder);
			meshShaderBindings[index]->SetConstantSet(meshShaderConstants[index]->GetName()
				, meshShaderConstants[index]);
			meshShaderBindings[index]->SetTexture("TestTexture", image);
			meshShaderBindings[index]->SetSampler("TestSampler", sampler);

			auto translation = glm::translate(glm::mat4(1.0f), glm::vec3(xx, yy, 0.0f) * 2.5f + glm::vec3(1.0f, 1.0f, 0.0f));
			meshShaderConstants[index]->SetValue("ObjectMatrix", translation);
			meshBatch.InsertMeshDrawcall(meshInfo
							, CPipelineStateObject{ DepthStencilStates::NormalOpaque() }
							, shaderSet
							, meshShaderBindings[index]);
		}
	}

	std::chrono::high_resolution_clock timer;
	auto lastTime = timer.now();
	float deltaTime = 0.0f;
	float rotation = 0.0f;
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

	while (pBackend->AnyWindowRunning())
	{
		auto windowSize = windowHandle2->GetSizeSafe();

		UpdateIMGUI(windowSize.x, windowSize.y);

		rotation += deltaTime * glm::radians(50.0f);
		auto rotMat = glm::rotate(glm::mat4(1), rotation, glm::vec3(0.0f, 0.0f, 1.0f));
		for (uint16_t xx = 0; xx < 4; ++xx)
		{
			for (uint16_t yy = 0; yy < 4; ++yy)
			{
				uint32_t index = xx * 4 + yy;
				auto translation = glm::translate(glm::mat4(1.0f), glm::vec3(xx, yy, 0.0f) * 2.5f + glm::vec3(1.0f, 1.0f, 0.0f));
				meshShaderConstants[index]->SetValue("ObjectMatrix", translation * rotMat);
			}
		}


		io.MousePos = ImVec2(windowHandle2->GetMouseX(), windowHandle2->GetMouseY());
		io.MouseDown[0] = windowHandle2->IsMouseDown(CA_MOUSE_BUTTON_LEFT);
		io.MouseDown[1] = windowHandle2->IsMouseDown(CA_MOUSE_BUTTON_RIGHT);
		io.MouseDown[2] = windowHandle2->IsMouseDown(CA_MOUSE_BUTTON_MIDDLE);

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
				if (!mouseDown)
				{
					lastMousePos = mousePos;
					mouseDown = true;
				}
				mouseDelta = mousePos - lastMousePos;
				lastMousePos = mousePos;
			}
			else// if(windowHandle2->IsMouseUp(CA_MOUSE_BUTTON_LEFT))
			{
				mouseDown = false;
			}

			auto windowSize1 = windowHandle2->GetSizeSafe();
			cam.Tick(deltaTime, forwarding, lefting, mouseDelta.x, mouseDelta.y, windowSize1.x, windowSize1.y);
			//std::cout << "Forward : " << forwarding << " Left : " << lefting << std::endl;
			//std::cout << "Mouse : " << mouseDelta.x << " " << mouseDelta.y << std::endl;

			drawInterface.Update(cam.GetViewProjMatrix());
			meshBatch.Update(cam.GetViewProjMatrix());

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
			targetattachmentInfo.clearValue = GraphicsClearValue::ClearColor(0.0f, 1.0f, 1.0f, 1.0f);

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
					, &drawInterface
				);


			ShaderBindingSetHandle blitBandingHandle = pRenderGraph->NewShaderBindingSetHandle(finalBlitBindingBuilder)
				.SetTexture("SourceTexture", colorTextureHandle)
				.SetSampler("SourceSampler", sampler);

			auto vertexBufferHandle = pRenderGraph->NewGPUBufferHandle(EBufferUsage::eVertexBuffer | EBufferUsage::eDataDst, vertexDataList.size(), sizeof(VertexData))
				.ScheduleBufferData(0, vertexDataList.size() * sizeof(VertexData), vertexDataList.data());
			auto indexBufferHandle = pRenderGraph->NewGPUBufferHandle(EBufferUsage::eIndexBuffer | EBufferUsage::eDataDst, indexDataList.size(), sizeof(uint16_t))
				.ScheduleBufferData(0, indexDataList.size() * sizeof(uint16_t), indexDataList.data());
			pRenderGraph->NewRenderPass({ targetattachmentInfo })
				.SetAttachmentTarget(0, windowBackBuffer)
				.Subpass({ {0} }
					, CPipelineStateObject{ }
					, vertexInputDesc
					, finalBlitShaderSet
					, { {}, {blitBandingHandle} }
					, [blitBandingHandle, vertexBufferHandle, indexBufferHandle](CInlineCommandList& cmd)
					{
						cmd.SetShaderBindings(std::vector<ShaderBindingSetHandle>{ blitBandingHandle })
							.BindVertexBuffers({ vertexBufferHandle }, {})
							.BindIndexBuffers(EIndexBufferType::e16, indexBufferHandle)
							.DrawIndexed(6);
					});

			DrawIMGUI(pRenderGraph, windowBackBuffer, imguiShaderSet, imguiConstantBuilder, imguiBindingBuilder, sampler, fontImage);
			pBackend->ExecuteRenderGraph(pRenderGraph);
		}

		pBackend->TickBackend();
		pBackend->TickWindows();
		++frame;
		auto currentTime = timer.now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime).count();
		lastTime = currentTime;
		deltaTime = duration / 1000.0f;
		deltaTime = std::max(deltaTime, 0.005f);
		float frameRate = 1.0f / deltaTime;
	}
	pBackend->Release();
	pBackend.reset();
	pThreadManager.reset();
	ImGui::DestroyContext();
	return EXIT_SUCCESS;
}
