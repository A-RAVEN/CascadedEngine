#include "VertexInputStateManager.h"
#include <RenderBackend_Vulkan.h>

namespace graphics_backend
{
	VertexInputState const& VertexInputStateManager::EnsureVertexInputState(VertexInputStatesCache const& cacheData)
	{
		TypedVKHashVal<VertexInputStatesCache> val = VKHashFunc<VertexInputStatesCache>(cacheData);
		auto resultItr = m_StateDesc.get_or_create(val, [&]()
		{
			VertexInputState newState;
			GetApp()->InitSubObj(&newState, cacheData);
			return newState;
		});
		return resultItr->second;
	}
	VertexInputState const& VertexInputStateManager::GetVertexInputState(TypedVKHashVal<VertexInputStatesCache> const& hashVal) const
	{
		auto result = m_StateDesc.try_get(hashVal);
		if (result != nullptr)
		{
			return *result;
		}
		throw std::runtime_error("VertexInputState not found");
	}
	void VertexInputState::Init(VertexInputStatesCache const& stateDesc)
	{
		m_StateCache = stateDesc;
		//Note: Vertex Input State is part of Pipeline Creation in Vulkan
		// Gated by BOTH compile-time constant and runtime device feature support —
		// the extension may be absent on the runtime device even when compiled in
		// (VK_EXT_graphics_pipeline_library feature query, see RenderBackend_Vulkan::Init).
		if (VULKAN_SUPPORT_PIPELINE_LIBRARY && GetApp()->IsPipelineLibrarySupported())
		{
			vk::GraphicsPipelineLibraryCreateInfoEXT libraryInfo{};
			libraryInfo.flags = vk::GraphicsPipelineLibraryFlagBitsEXT::eVertexInputInterface;

			vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
			vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
			stateDesc.GetPipelineInputAssemblyStateCreateInfo(inputAssemblyInfo);
			stateDesc.GetPipelineVertexInputStateCreateInfo(vertexInputInfo);

			vk::GraphicsPipelineCreateInfo pipelineCreateInfo{};
			pipelineCreateInfo.pNext = &libraryInfo;
			// F29: a pipeline with a VkGraphicsPipelineLibraryCreateInfoEXT subset must set
			// VK_PIPELINE_CREATE_LIBRARY_BIT_EXT. (VUID note: 06606 reads the opposite way —
			// LIBRARY_BIT must NOT be set when the graphicsPipelineLibrary feature is
			// disabled; this branch is gated by the runtime feature check, task 1.3.)
			pipelineCreateInfo.flags = vk::PipelineCreateFlagBits::eLibraryKHR;
			pipelineCreateInfo.setPInputAssemblyState(&inputAssemblyInfo);
			pipelineCreateInfo.setPVertexInputState(&vertexInputInfo);
			m_VertexInputState = GetDevice().createGraphicsPipeline(GetApp()->GetPipelineCache(), pipelineCreateInfo).value;
		}
	}
}