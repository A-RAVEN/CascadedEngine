#include <PipelineLibrary/VulkanPipelineLibrary.h>
#include <RenderBackend_Vulkan.h>
#include <Utils/VulkanDebug.h>

namespace graphics_backend
{
	void VulkanPipelineLibrary::Init()
	{
		// Check if VK_EXT_graphics_pipeline_library is supported
		m_Supported = GetApp()->IsPipelineLibrarySupported();

		if (m_Supported)
		{
			CA_LOG_INFO("VulkanPipelineLibrary initialized with VK_EXT_graphics_pipeline_library support");
		}
		else
		{
			CA_LOG_INFO("VulkanPipelineLibrary initialized (using monolithic fallback)");
		}
	}

	void VulkanPipelineLibrary::Release()
	{
		auto device = GetDevice();

		for (auto pipeline : m_CreatedPipelines)
		{
			if (pipeline)
			{
				device.destroyPipeline(pipeline);
			}
		}
		m_CreatedPipelines.clear();
		m_LibraryCache.clear();

		CA_LOG_INFO("VulkanPipelineLibrary released");
	}

	vk::Pipeline VulkanPipelineLibrary::CreateVertexInputLibrary(
		vk::PipelineVertexInputStateCreateInfo const& vertexInputState,
		vk::PipelineInputAssemblyStateCreateInfo const& inputAssemblyState,
		vk::PipelineCache cache)
	{
		if (!m_Supported)
		{
			CA_LOG_WARN("VulkanPipelineLibrary: Pipeline library not supported, returning null");
			return nullptr;
		}

		auto device = GetDevice();

		vk::GraphicsPipelineLibraryCreateInfoEXT libraryInfo{};
		libraryInfo.flags = vk::GraphicsPipelineLibraryFlagBitsEXT::eVertexInputInterface;

		vk::GraphicsPipelineCreateInfo createInfo{};
		createInfo.pNext = &libraryInfo;
		createInfo.flags = vk::PipelineCreateFlagBits::eLibraryKHR | vk::PipelineCreateFlagBits::eRetainLinkTimeOptimizationInfoEXT;
		createInfo.pVertexInputState = &vertexInputState;
		createInfo.pInputAssemblyState = &inputAssemblyState;

		try
		{
			auto pipeline = device.createGraphicsPipeline(cache, createInfo).value;
			m_CreatedPipelines.push_back(pipeline);
			CA_LOG_INFO("VulkanPipelineLibrary: Created vertex input library");
			return pipeline;
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanPipelineLibrary: Failed to create vertex input library: {}", e.what());
			return nullptr;
		}
	}

	vk::Pipeline VulkanPipelineLibrary::CreatePreRasterizationLibrary(
		vk::PipelineShaderStageCreateInfo const& vertexShader,
		vk::PipelineShaderStageCreateInfo const* tessControlShader,
		vk::PipelineShaderStageCreateInfo const* tessEvalShader,
		vk::PipelineShaderStageCreateInfo const* geometryShader,
		vk::PipelineLayout layout,
		vk::PipelineViewportStateCreateInfo const& viewportState,
		vk::PipelineRasterizationStateCreateInfo const& rasterizationState,
		vk::PipelineDynamicStateCreateInfo const* pDynamicState,
		vk::PipelineCache cache)
	{
		if (!m_Supported)
		{
			return nullptr;
		}

		auto device = GetDevice();

		if (!vertexShader.module)
		{
			CA_LOG_ERR("VulkanPipelineLibrary: Vertex shader module is null");
			return nullptr;
		}

		castl::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
		shaderStages.push_back(vertexShader);
		if (tessControlShader) shaderStages.push_back(*tessControlShader);
		if (tessEvalShader) shaderStages.push_back(*tessEvalShader);
		if (geometryShader) shaderStages.push_back(*geometryShader);

		vk::GraphicsPipelineLibraryCreateInfoEXT libraryInfo{};
		libraryInfo.flags = vk::GraphicsPipelineLibraryFlagBitsEXT::ePreRasterizationShaders;

		vk::GraphicsPipelineCreateInfo createInfo{};
		createInfo.pNext = &libraryInfo;
		createInfo.flags = vk::PipelineCreateFlagBits::eLibraryKHR | vk::PipelineCreateFlagBits::eRetainLinkTimeOptimizationInfoEXT;
		createInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		createInfo.pStages = shaderStages.data();
		createInfo.layout = layout;
		createInfo.pViewportState = &viewportState;
		createInfo.pRasterizationState = &rasterizationState;
		createInfo.pDynamicState = pDynamicState;

		try
		{
			auto pipeline = device.createGraphicsPipeline(cache, createInfo).value;
			m_CreatedPipelines.push_back(pipeline);
			CA_LOG_INFO("VulkanPipelineLibrary: Created pre-rasterization library");
			return pipeline;
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanPipelineLibrary: Failed to create pre-rasterization library: {}", e.what());
			return nullptr;
		}
	}

	vk::Pipeline VulkanPipelineLibrary::CreateFragmentLibrary(
		vk::PipelineShaderStageCreateInfo const& fragmentShader,
		vk::PipelineLayout layout,
		vk::RenderPass renderPass,
		vk::PipelineDepthStencilStateCreateInfo const* pDepthStencilState,
		vk::PipelineCache cache)
	{
		if (!m_Supported)
		{
			return nullptr;
		}

		auto device = GetDevice();

		if (!fragmentShader.module)
		{
			CA_LOG_ERR("VulkanPipelineLibrary: Fragment shader module is null");
			return nullptr;
		}

		vk::GraphicsPipelineLibraryCreateInfoEXT libraryInfo{};
		libraryInfo.flags = vk::GraphicsPipelineLibraryFlagBitsEXT::eFragmentShader;

		vk::GraphicsPipelineCreateInfo createInfo{};
		createInfo.pNext = &libraryInfo;
		createInfo.flags = vk::PipelineCreateFlagBits::eLibraryKHR | vk::PipelineCreateFlagBits::eRetainLinkTimeOptimizationInfoEXT;
		createInfo.stageCount = 1;
		createInfo.pStages = &fragmentShader;
		createInfo.layout = layout;
		createInfo.renderPass = renderPass;
		createInfo.pDepthStencilState = pDepthStencilState;

		try
		{
			auto pipeline = device.createGraphicsPipeline(cache, createInfo).value;
			m_CreatedPipelines.push_back(pipeline);
			CA_LOG_INFO("VulkanPipelineLibrary: Created fragment library");
			return pipeline;
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanPipelineLibrary: Failed to create fragment library: {}", e.what());
			return nullptr;
		}
	}

	vk::Pipeline VulkanPipelineLibrary::CreateFragmentOutputLibrary(
		vk::PipelineMultisampleStateCreateInfo const& multisampleState,
		vk::PipelineDepthStencilStateCreateInfo const* depthStencilState,
		vk::PipelineColorBlendStateCreateInfo const& colorBlendState,
		vk::RenderPass renderPass,
		vk::PipelineCache cache)
	{
		if (!m_Supported)
		{
			return nullptr;
		}

		auto device = GetDevice();

		vk::GraphicsPipelineLibraryCreateInfoEXT libraryInfo{};
		libraryInfo.flags = vk::GraphicsPipelineLibraryFlagBitsEXT::eFragmentOutputInterface;

		vk::GraphicsPipelineCreateInfo createInfo{};
		createInfo.pNext = &libraryInfo;
		createInfo.flags = vk::PipelineCreateFlagBits::eLibraryKHR | vk::PipelineCreateFlagBits::eRetainLinkTimeOptimizationInfoEXT;
		createInfo.pMultisampleState = &multisampleState;
		createInfo.pDepthStencilState = depthStencilState;
		createInfo.pColorBlendState = &colorBlendState;
		createInfo.renderPass = renderPass;

		try
		{
			auto pipeline = device.createGraphicsPipeline(cache, createInfo).value;
			m_CreatedPipelines.push_back(pipeline);
			CA_LOG_INFO("VulkanPipelineLibrary: Created fragment output library");
			return pipeline;
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanPipelineLibrary: Failed to create fragment output library: {}", e.what());
			return nullptr;
		}
	}

	vk::Pipeline VulkanPipelineLibrary::LinkPipeline(
		PipelineLibraryParts const& libraries,
		vk::PipelineLayout layout,
		vk::RenderPass renderPass,
		uint32_t subpass,
		vk::PipelineCache cache)
	{
		if (!m_Supported)
		{
			CA_LOG_WARN("VulkanPipelineLibrary: Cannot link - library not supported");
			return nullptr;
		}

		auto device = GetDevice();

		castl::vector<vk::Pipeline> librariesToLink;
		if (libraries.vertexInputLibrary)
			librariesToLink.push_back(libraries.vertexInputLibrary);
		if (libraries.preRasterizationLibrary)
			librariesToLink.push_back(libraries.preRasterizationLibrary);
		if (libraries.fragmentLibrary)
			librariesToLink.push_back(libraries.fragmentLibrary);
		if (libraries.fragmentOutputLibrary)
			librariesToLink.push_back(libraries.fragmentOutputLibrary);

		if (librariesToLink.empty())
		{
			CA_LOG_ERR("VulkanPipelineLibrary: No libraries to link");
			return nullptr;
		}

		vk::PipelineLibraryCreateInfoKHR libraryLinkInfo{};
		libraryLinkInfo.libraryCount = static_cast<uint32_t>(librariesToLink.size());
		libraryLinkInfo.pLibraries = librariesToLink.data();

		vk::GraphicsPipelineCreateInfo createInfo{};
		createInfo.pNext = &libraryLinkInfo;
		createInfo.flags = vk::PipelineCreateFlagBits::eLinkTimeOptimizationEXT;
		createInfo.layout = layout;
		createInfo.renderPass = renderPass;
		createInfo.subpass = subpass;

		try
		{
			auto pipeline = device.createGraphicsPipeline(cache, createInfo).value;
			m_CreatedPipelines.push_back(pipeline);
			CA_LOG_INFO("VulkanPipelineLibrary: Linked pipeline from {} libraries", librariesToLink.size());
			return pipeline;
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanPipelineLibrary: Failed to link pipeline: {}", e.what());
			return nullptr;
		}
	}

	vk::Pipeline VulkanPipelineLibrary::CreateMonolithicPipeline(
		vk::GraphicsPipelineCreateInfo const& createInfo,
		vk::PipelineCache cache)
	{
		auto device = GetDevice();

		try
		{
			auto pipeline = device.createGraphicsPipeline(cache, createInfo).value;
			m_CreatedPipelines.push_back(pipeline);
			CA_LOG_INFO("VulkanPipelineLibrary: Created monolithic pipeline");
			return pipeline;
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanPipelineLibrary: Failed to create monolithic pipeline: {}", e.what());
			return nullptr;
		}
	}

	PipelineLibraryParts const* VulkanPipelineLibrary::GetCachedLibrary(PipelineLibraryKey const& key) const
	{
		for (auto const& [cacheKey, parts] : m_LibraryCache)
		{
			if (cacheKey == key)
			{
				return &parts;
			}
		}
		return nullptr;
	}

	void VulkanPipelineLibrary::CacheLibrary(PipelineLibraryKey const& key, PipelineLibraryParts const& parts)
	{
		m_LibraryCache.push_back({ key, parts });
	}
}
