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
			SetVKObjectDebugName(GetDevice(), pipeline, "Pipeline:VertexInput");
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

		// VUID-pRasterizationState-09039: a PRE_RASTERIZATION_SHADERS library must provide
		// pMultisampleState (rasterization state is only valid with multisample state).
		vk::PipelineMultisampleStateCreateInfo multisampleState{};
		multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;

		// VUID-pStages-09022: a pipeline whose pStages includes a tessellation control
		// stage must provide pTessellationState.
		// F26a: VUID-VkPipelineTessellationStateCreateInfo-patchControlPoints-01214 —
		// patchControlPoints must be > 0 (tessellation implies PATCH_LIST topology;
		// 3 control points per patch is the conventional default).
		vk::PipelineTessellationStateCreateInfo tessellationState{};
		tessellationState.patchControlPoints = 3;

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
		createInfo.pMultisampleState = &multisampleState;
		if (tessControlShader || tessEvalShader)
		{
			createInfo.pTessellationState = &tessellationState;
		}
		createInfo.pDynamicState = pDynamicState;

		try
		{
			auto pipeline = device.createGraphicsPipeline(cache, createInfo).value;
			m_CreatedPipelines.push_back(pipeline);
			SetVKObjectDebugName(GetDevice(), pipeline, "Pipeline:PreRasterization");
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

		// VUID-renderPass-09028: a fragment-shader-state pipeline whose subpass uses a
		// depth/stencil attachment must provide a valid pDepthStencilState. Default to a
		// disabled depth/stencil state when the caller passes null.
		vk::PipelineDepthStencilStateCreateInfo defaultDepthStencil{};
		if (!pDepthStencilState)
		{
			defaultDepthStencil.depthTestEnable = VK_FALSE;
			defaultDepthStencil.depthWriteEnable = VK_FALSE;
			pDepthStencilState = &defaultDepthStencil;
		}

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
			SetVKObjectDebugName(GetDevice(), pipeline, "Pipeline:Fragment");
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
			SetVKObjectDebugName(GetDevice(), pipeline, "Pipeline:FragmentOutput");
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

		// F27: basic link-consistency validation. Full layout/renderPass identity between
		// libraries and the link info (VUID-flags-06612 etc.) is not introspectable from
		// pipeline handles, but the link parameters themselves must be coherent: a fragment
		// library in the link requires a non-null renderPass (its subpass attachments are
		// baked into the library), and the link layout must be valid.
		if (layout == vk::PipelineLayout{})
		{
			CA_LOG_ERR("VulkanPipelineLibrary: LinkPipeline requires a valid pipeline layout");
			return nullptr;
		}
		if (libraries.fragmentLibrary && !renderPass)
		{
			CA_LOG_ERR("VulkanPipelineLibrary: LinkPipeline: fragment library requires a non-null renderPass");
			return nullptr;
		}
		// F27a (coverage note): the fragment-output library also bakes renderPass/subpass
		// into the pipeline — a non-null renderPass is a hard requirement when it is in
		// the link, and the link renderPass must match the library's. Pipeline handles
		// cannot be introspected for renderPass identity, so this remains a documented
		// caller contract rather than a runtime check.

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
			SetVKObjectDebugName(GetDevice(), pipeline, "Pipeline:Linked");
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
			SetVKObjectDebugName(GetDevice(), pipeline, "Pipeline:Monolithic");
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
		auto it = m_LibraryCache.find(key);
		if (it != m_LibraryCache.end())
		{
			return &it->second;
		}
		return nullptr;
	}

	void VulkanPipelineLibrary::CacheLibrary(PipelineLibraryKey const& key, PipelineLibraryParts const& parts)
	{
		m_LibraryCache.insert({ key, parts });
	}
}
