#include <GPUTexture.h>
#include <imgui.h>
#include <glm/mat4x4.hpp>
#include "IMGUIContext.h"
#include "ShaderResource.h"
#include "KeyCodes.h"

using namespace graphics_backend;
using namespace resource_management;
IMGUIContext::IMGUIContext() : 
	m_ImguiShaderConstantsBuilder("IMGUIConstants"),
	m_ImguiShaderBindingBuilder("IMGUIBinding")
{
	m_ImguiShaderConstantsBuilder
		.Vec2<float>("IMGUIScale");
	m_ImguiShaderBindingBuilder.ConstantBuffer(m_ImguiShaderConstantsBuilder)
		.Texture2D<float, 4>("FontTexture")
		.SamplerState("FontSampler");
}
void IMGUIContext::Initialize(
	CRenderBackend* renderBackend
	, ResourceManagingSystem* resourceSystem
)
{
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
	m_Fontimage = renderBackend->CreateGPUTexture(fontDesc);
	m_Fontimage->ScheduleTextureData(0, texWidth * texHeight * 4, fontData);

	m_ImageSampler = renderBackend->GetOrCreateTextureSampler(TextureSamplerDescriptor{});

	resourceSystem->LoadResource<ShaderResrouce>("Shaders/Imgui.shaderbundle", [this](ShaderResrouce* result)
		{
			m_ImguiShaderSet.vert = &result->m_VertexShaderProvider;
			m_ImguiShaderSet.frag = &result->m_FragmentShaderProvider;
		});
}

void IMGUIContext::Release()
{
	m_ImageSampler.reset();
	m_Fontimage.reset();
	ImGui::DestroyContext();
}

void IMGUIContext::UpdateIMGUI(graphics_backend::WindowHandle const* windowHandle)
{
	auto& io = ImGui::GetIO();
	auto windowSize = windowHandle->GetSizeSafe();

	io.DisplaySize = ImVec2(windowSize.x, windowSize.y);
	io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
	ImGui::NewFrame();
	//SRS - ShowDemoWindow() sets its own initial position and size, cannot override here
	ImGui::ShowDemoWindow();

	io.MousePos = ImVec2(windowHandle->GetMouseX(), windowHandle->GetMouseY());
	io.MouseDown[0] = windowHandle->IsMouseDown(CA_MOUSE_BUTTON_LEFT);
	io.MouseDown[1] = windowHandle->IsMouseDown(CA_MOUSE_BUTTON_RIGHT);
	io.MouseDown[2] = windowHandle->IsMouseDown(CA_MOUSE_BUTTON_MIDDLE);
	// Render to generate draw buffers
	ImGui::Render();
}

void IMGUIContext::DrawIMGUI(graphics_backend::CRenderGraph* renderGraph, graphics_backend::TextureHandle renderTargethandle)
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
	auto indexBufferHandle = renderGraph->NewGPUBufferHandle(EBufferUsage::eIndexBuffer | EBufferUsage::eDataDst, imDrawData->TotalIdxCount, sizeof(ImDrawIdx));

	CVertexInputDescriptor vertexInputDesc{};
	vertexInputDesc.AddPrimitiveDescriptor(sizeof(ImDrawVert), {
		VertexAttribute{0, offsetof(ImDrawVert, pos), VertexInputFormat::eR32G32_SFloat}
		, VertexAttribute{1, offsetof(ImDrawVert, uv), VertexInputFormat::eR32G32_SFloat}
		, VertexAttribute{2, offsetof(ImDrawVert, col), VertexInputFormat::eR8G8B8A8_UNorm}
		});

	size_t vtxOffset = 0;
	size_t idxOffset = 0;
	castl::vector<castl::tuple<uint32_t, uint32_t, uint32_t>> indexDataOffsets;
	castl::vector<glm::uvec4> sissors;
	for (int n = 0; n < imDrawData->CmdListsCount; n++) {
		const ImDrawList* cmd_list = imDrawData->CmdLists[n];
		renderGraph->ScheduleBufferData(vertexBufferHandle, vtxOffset * sizeof(ImDrawVert), cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), cmd_list->VtxBuffer.Data);
		renderGraph->ScheduleBufferData(indexBufferHandle, idxOffset * sizeof(ImDrawIdx), cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), cmd_list->IdxBuffer.Data);

		for (int j = 0; j < cmd_list->CmdBuffer.Size; ++j)
		{
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
			indexDataOffsets.push_back(castl::make_tuple(idxOffset, vtxOffset, pcmd->ElemCount));
			sissors.push_back(glm::ivec4(pcmd->ClipRect.x, pcmd->ClipRect.y, pcmd->ClipRect.z - pcmd->ClipRect.x, pcmd->ClipRect.w - pcmd->ClipRect.y));
			idxOffset += pcmd->ElemCount;
		}
		vtxOffset += cmd_list->VtxBuffer.Size;
	}

	CAttachmentInfo targetattachmentInfo{};
	targetattachmentInfo.format = renderGraph->GetTextureDescriptor(renderTargethandle).format;
	auto shaderConstants = renderGraph->NewShaderConstantSetHandle(m_ImguiShaderConstantsBuilder);
	auto shaderBinding = renderGraph->NewShaderBindingSetHandle(m_ImguiShaderBindingBuilder);
	shaderConstants.SetValue("IMGUIScale", meshScale);
	shaderBinding.SetConstantSet(m_ImguiShaderConstantsBuilder.GetName(), shaderConstants)
		.SetSampler("FontSampler", m_ImageSampler)
		.SetTexture("FontTexture", m_Fontimage);
	auto blendStates = ColorAttachmentsBlendStates::AlphaTransparent();
	renderGraph->NewRenderPass({ targetattachmentInfo })
		.SetAttachmentTarget(0, renderTargethandle)
		.Subpass({ {0} }
			, CPipelineStateObject{ {}, RasterizerStates::CullOff(), blendStates }
			, vertexInputDesc
			, m_ImguiShaderSet
			, { {}, {shaderBinding} }
			, [shaderBinding, vertexBufferHandle, indexBufferHandle, indexDataOffsets, sissors](CInlineCommandList& cmd)
			{
				cmd.SetShaderBindings(castl::vector<ShaderBindingSetHandle>{ shaderBinding })
					.BindVertexBuffers({ vertexBufferHandle })
					.BindIndexBuffers(EIndexBufferType::e16, indexBufferHandle);
				for (uint32_t i = 0; i < indexDataOffsets.size(); ++i)
				{
					cmd.SetSissor(sissors[i].x, sissors[i].y, sissors[i].z, sissors[i].w)
						.DrawIndexed(castl::get<2>(indexDataOffsets[i]), 1, castl::get<0>(indexDataOffsets[i]), castl::get<1>(indexDataOffsets[i]));
				}
			});
}
