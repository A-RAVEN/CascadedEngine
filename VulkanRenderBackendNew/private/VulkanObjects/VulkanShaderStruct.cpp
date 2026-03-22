#include <VulkanObjects/VulkanShaderStruct.h>
#include <RenderBackend_Vulkan.h>

namespace graphics_backend
{
	void VulkanShaderStruct::Init(cacore::NameHash const& structType)
	{
		m_StructTypeName = structType;

		// Create a basic descriptor set layout for now
		// In a full implementation, this would be populated from shader reflection
		vk::DescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.bindingCount = 0;
		layoutInfo.pBindings = nullptr;

		m_DescriptorSetLayout = GetDevice().createDescriptorSetLayout(layoutInfo);

		// Create pipeline layout
		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;

		m_PipelineLayout = GetDevice().createPipelineLayout(pipelineLayoutInfo);

		// Create descriptor pool
		vk::DescriptorPoolSize poolSizes[] = {
			{ vk::DescriptorType::eUniformBuffer, 100 },
			{ vk::DescriptorType::eCombinedImageSampler, 100 },
			{ vk::DescriptorType::eStorageBuffer, 100 },
		};

		vk::DescriptorPoolCreateInfo poolInfo{};
		poolInfo.maxSets = 100;
		poolInfo.poolSizeCount = 3;
		poolInfo.pPoolSizes = poolSizes;

		m_DescriptorPool = GetDevice().createDescriptorPool(poolInfo);

		// Allocate descriptor set
		vk::DescriptorSetAllocateInfo allocInfo{};
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_DescriptorSetLayout;

		auto sets = GetDevice().allocateDescriptorSets(allocInfo);
		if (!sets.empty())
		{
			m_DescriptorSet = sets[0];
		}

		CA_LOG_INFO("VulkanShaderStruct initialized for type: {}", structType.ToString().c_str());
	}

	void VulkanShaderStruct::Release()
	{
		auto device = GetDevice();

		if (m_DescriptorSet)
		{
			device.freeDescriptorSets(m_DescriptorPool, m_DescriptorSet);
			m_DescriptorSet = nullptr;
		}

		if (m_DescriptorPool)
		{
			device.destroyDescriptorPool(m_DescriptorPool);
			m_DescriptorPool = nullptr;
		}

		if (m_PipelineLayout)
		{
			device.destroyPipelineLayout(m_PipelineLayout);
			m_PipelineLayout = nullptr;
		}

		if (m_DescriptorSetLayout)
		{
			device.destroyDescriptorSetLayout(m_DescriptorSetLayout);
			m_DescriptorSetLayout = nullptr;
		}

		m_PendingUpdates.clear();
	}

	void VulkanShaderStruct::SetValueInternal(cacore::NameHash const& name
		, void const* pValue
		, uint32_t sizeInBytes
		, uint32_t elementIndex)
	{
		PendingUpdate update{};
		update.type = PendingUpdate::Type::Value;
		update.name = name;
		update.elementIndex = elementIndex;
		update.data.resize(sizeInBytes);
		memcpy(update.data.data(), pValue, sizeInBytes);
		m_PendingUpdates.push_back(castl::move(update));
	}

	void VulkanShaderStruct::SetImageInternal(cacore::NameHash const& name
		, ImageHandle const& imageHandle, GPUTextureView const& view
		, uint32_t elementIndex)
	{
		PendingUpdate update{};
		update.type = PendingUpdate::Type::Image;
		update.name = name;
		update.elementIndex = elementIndex;
		update.imageHandle = imageHandle;
		update.textureView = view;
		m_PendingUpdates.push_back(castl::move(update));
	}

	void VulkanShaderStruct::SetBufferInternal(cacore::NameHash const& name
		, BufferHandle const& bufferHandle
		, uint32_t elementIndex)
	{
		PendingUpdate update{};
		update.type = PendingUpdate::Type::Buffer;
		update.name = name;
		update.elementIndex = elementIndex;
		update.bufferHandle = bufferHandle;
		m_PendingUpdates.push_back(castl::move(update));
	}

	void VulkanShaderStruct::SetSamplerInternal(cacore::NameHash const& name
		, TextureSamplerDescriptor const& samplerDesc
		, uint32_t elementIndex)
	{
		PendingUpdate update{};
		update.type = PendingUpdate::Type::Sampler;
		update.name = name;
		update.elementIndex = elementIndex;
		update.samplerDesc = samplerDesc;
		m_PendingUpdates.push_back(castl::move(update));
	}

	void VulkanShaderStruct::SetStructInternal(cacore::NameHash const& name
		, castl::shared_ptr<ShaderStruct> const& subStruct
		, uint32_t elementIndex)
	{
		PendingUpdate update{};
		update.type = PendingUpdate::Type::Struct;
		update.name = name;
		update.elementIndex = elementIndex;
		update.subStruct = subStruct;
		m_PendingUpdates.push_back(castl::move(update));
	}

	void VulkanShaderStruct::FlushUpdates()
	{
		// Process pending updates and write to descriptor sets
		// This is a simplified implementation - full version would handle all update types
		for (auto const& update : m_PendingUpdates)
		{
			switch (update.type)
			{
			case PendingUpdate::Type::Value:
				// Write to uniform buffer
				break;
			case PendingUpdate::Type::Image:
				// Write image descriptor
				break;
			case PendingUpdate::Type::Buffer:
				// Write buffer descriptor
				break;
			case PendingUpdate::Type::Sampler:
				// Write sampler descriptor
				break;
			case PendingUpdate::Type::Struct:
				// Handle nested struct
				break;
			}
		}
		m_PendingUpdates.clear();
	}
}
