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
		if (VULKAN_SUPPORT_PIPELINE_LIBRARY)
		{
			vk::GraphicsPipelineLibraryCreateInfoEXT libraryInfo{};
			libraryInfo.flags = vk::GraphicsPipelineLibraryFlagBitsEXT::eVertexInputInterface;

			vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
			vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
			stateDesc.GetPipelineInputAssemblyStateCreateInfo(inputAssemblyInfo);
			stateDesc.GetPipelineVertexInputStateCreateInfo(vertexInputInfo);

			vk::GraphicsPipelineCreateInfo pipelineCreateInfo{};
			pipelineCreateInfo.pNext = &libraryInfo;
			pipelineCreateInfo.setPInputAssemblyState(&inputAssemblyInfo);
			pipelineCreateInfo.setPVertexInputState(&vertexInputInfo);
			m_VertexInputState = GetDevice().createGraphicsPipeline(nullptr, pipelineCreateInfo).value;
		}
	}
}