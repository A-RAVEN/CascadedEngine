#include <GPUGraph/VulkanResourceBindingInstance.h>
#include <GPUGraph/VulkanGraphExecutor.h>
#include <VulkanObjects/VulkanShaderStruct.h>
#include <RenderBackend_Vulkan.h>

namespace graphics_backend
{
	// ReflectResourceBindings: Walk the shader binding hierarchy (BFS) and invoke callback for each binding pair
	// References D3D12 ReflectResourceBindings in GPUResourceBindingInstance.cpp
	struct VulkanBindingPair
	{
		VulkanStructBindingInfo const* pBindingInfo;
		VulkanShaderStruct const* pStruct;
		uint32_t bindingOffset;
	};

	static void ReflectResourceBindings(VulkanShaderResourceBindingInfo const& bindingData
		, castl::unordered_map<cacore::NameHash, VulkanShaderStruct const*> const& resourceBindings
		, castl::function<void(VulkanBindingPair const&)> hierarchyBoundCallback)
	{
		castl::deque<VulkanBindingPair> hierarchies;

		// Start from root struct binding
		auto& rootBinding = bindingData.structBindingInfos[0];
		for (uint32_t id = 0; id < rootBinding.subStructCount; ++id)
		{
			auto findRootShaderStruct = [&](cacore::NameHash const& inName) -> VulkanShaderStruct const*
			{
				auto found = resourceBindings.find(inName);
				if (found != resourceBindings.end())
				{
					return found->second;
				}
				CA_LOG_ERR_BREAK("Root Shader Struct [{}] Not Found", inName);
				return nullptr;
			};

			uint32_t subBindingID = rootBinding.subStructOffset + id;
			auto& subBindingInfo = bindingData.structBindingInfos[subBindingID];
			hierarchies.push_back(VulkanBindingPair{ &subBindingInfo, findRootShaderStruct(subBindingInfo.structBindingName), 0 });
		}

		// BFS traversal
		while (!hierarchies.empty())
		{
			VulkanBindingPair bindingPair = hierarchies.front();
			hierarchyBoundCallback(bindingPair);
			hierarchies.pop_front();

			if (bindingPair.pBindingInfo != nullptr && bindingPair.pStruct != nullptr)
			{
				auto& itrHierarchy = *bindingPair.pBindingInfo;
				auto& itrStruct = *bindingPair.pStruct;
				uint32_t offset = bindingPair.bindingOffset;

				auto findShaderStructsFromParent = [&](cacore::NameHash const& inName)
					-> castl::vector<castl::shared_ptr<VulkanShaderStruct>> const*
				{
					auto found = itrStruct.GetSubStructs().find(inName);
					if (found != itrStruct.GetSubStructs().end())
					{
						return &found->second;
					}
					CA_LOG_ERR_BREAK("Shader Struct [{}] Not Found in Parent Struct [{}][type {}]"
						, inName
						, itrHierarchy.structBindingName
						, itrStruct.GetStructTypeName());
					return nullptr;
				};

				for (uint32_t id = 0; id < itrHierarchy.subStructCount; ++id)
				{
					uint32_t subBindingID = itrHierarchy.subStructOffset + id;
					auto& subHierarchy = bindingData.structBindingInfos[subBindingID];
					castl::vector<castl::shared_ptr<VulkanShaderStruct>> const* pStructs = findShaderStructsFromParent(subHierarchy.structBindingName);
					auto tryGetpStruct = [&](uint32_t elementID) -> VulkanShaderStruct const*
					{
						if (pStructs == nullptr)
							return nullptr;
						castl::vector<castl::shared_ptr<VulkanShaderStruct>> const& structs = *pStructs;
						if (structs.size() <= elementID)
							return nullptr;
						return static_cast<VulkanShaderStruct const*>(structs[elementID].get());
					};
					for (uint32_t elementID = 0; elementID < subHierarchy.elementCount; ++elementID)
					{
						uint32_t resolvedOffset = elementID + offset * subHierarchy.elementCount;
						VulkanShaderStruct const* itrSubStruct = tryGetpStruct(elementID);
						hierarchies.push_back(VulkanBindingPair{ &subHierarchy, itrSubStruct, resolvedOffset });
					}
				}
			}
		}
	}

	void VulkanResourceBindingInstance::Init(VulkanShaderResourceSet const& resourceSet)
	{
		m_DescriptorSets.clear();
		m_PendingWrites.clear();
		m_BufferInfos.clear();
		m_ImageInfos.clear();
		m_CBufferBindings.clear();
		m_ImageBindings.clear();
		m_BufferBindings.clear();
		m_SamplerBindings.clear();

		p_ShaderFileInfo = GetApp()->GetShaderFileInfo(resourceSet.shaderInfo);
		if (p_ShaderFileInfo == nullptr)
		{
			CA_LOG_WARN("VulkanResourceBindingInstance::Init - shader file info not found for {}", resourceSet.shaderInfo.path.GetHash());
			return;
		}

		auto& shaderBindingInfo = p_ShaderFileInfo->shaderBindingInfo;

		ReflectResourceBindings(shaderBindingInfo, resourceSet.resourceDic, [&](VulkanBindingPair const& hierarchyBound)
		{
			CA_ASSERT_BREAK(hierarchyBound.pBindingInfo != nullptr, "Invalid Hierarchy");
			CA_ASSERT_BREAK(hierarchyBound.pStruct != nullptr, "Struct Not Bound {}", hierarchyBound.pBindingInfo->structBindingName);

			auto& structBindingInfo = *hierarchyBound.pBindingInfo;
			auto pStruct = hierarchyBound.pStruct;
			uint32_t offset = hierarchyBound.bindingOffset;

			// CBuffer bindings
			for (auto cbufferID : structBindingInfo.cbufferRefs)
			{
				auto& bindingInfo = shaderBindingInfo.cbufferInfos[cbufferID];
				CBufferBindingElement cbufferElement{};
				cbufferElement.bindingInfo = bindingInfo;
				cbufferElement.offset = offset;
				cbufferElement.pCBufferStruct = pStruct;
				m_CBufferBindings.push_back(cbufferElement);
			}

			// Image bindings
			auto& imageHandles = pStruct->GetImageHandles();
			for (auto imageID : structBindingInfo.imageRefs)
			{
				auto& bindingInfo = shaderBindingInfo.imageInfos[imageID];
				auto found = imageHandles.find(bindingInfo.imageBindingName);
				CA_ASSERT_BREAK(found != imageHandles.end(), "Texture Not Found:{}", bindingInfo.imageBindingName);
				auto& imageList = found->second;
				ImageBindingElement imageElement{};
				imageElement.bindingInfo = bindingInfo;
				imageElement.offset = offset;
				imageElement.resourceUsages = bindingInfo.isUAV() ? EResourceUsage::eShaderUnorderedAccess : EResourceUsage::eShaderResource;
				for (uint32_t imgID = 0; imgID < imageList.size(); ++imgID)
				{
					imageElement.bindings.push_back(imageList[imgID]);
				}
				m_ImageBindings.push_back(imageElement);
			}

			// Buffer bindings
			auto& bufferHandles = pStruct->GetBufferHandles();
			for (auto bufferID : structBindingInfo.bufferRefs)
			{
				auto& bindingInfo = shaderBindingInfo.bufferInfos[bufferID];
				auto found = bufferHandles.find(bindingInfo.bufferBindingName);
				CA_ASSERT_BREAK(found != bufferHandles.end(), "Buffer Not Found:{}", bindingInfo.bufferBindingName);
				auto& bufferList = found->second;
				BufferBindingElement bufferElement{};
				bufferElement.bindingInfo = bindingInfo;
				bufferElement.offset = offset;
				bufferElement.resourceUsages = bindingInfo.isUAV() ? EResourceUsage::eShaderUnorderedAccess : EResourceUsage::eShaderResource;
				for (uint32_t bufID = 0; bufID < bufferList.size(); ++bufID)
				{
					bufferElement.bindings.push_back(bufferList[bufID]);
				}
				m_BufferBindings.push_back(bufferElement);
			}

			// Sampler bindings
			auto& samplerDescs = pStruct->GetSamplerDescriptors();
			for (auto samplerID : structBindingInfo.samplerRefs)
			{
				auto& bindingInfo = shaderBindingInfo.samplerInfos[samplerID];
				auto found = samplerDescs.find(bindingInfo.samplerBindingName);
				CA_ASSERT_BREAK(found != samplerDescs.end(), "Sampler Not Found:{}", bindingInfo.samplerBindingName);
				auto& samplerList = found->second;
				SamplerBindingElement samplerElement{};
				samplerElement.bindingInfo = bindingInfo;
				samplerElement.offset = offset;
				for (uint32_t smpID = 0; smpID < samplerList.size(); ++smpID)
				{
					samplerElement.samplerDescriptors.push_back(samplerList[smpID]);
				}
				m_SamplerBindings.push_back(samplerElement);
			}
		});
	}

	void VulkanResourceBindingInstance::Release()
	{
		m_DescriptorSets.clear();
		m_PendingWrites.clear();
		m_BufferInfos.clear();
		m_ImageInfos.clear();
		m_CBufferBindings.clear();
		m_ImageBindings.clear();
		m_BufferBindings.clear();
		m_SamplerBindings.clear();
		p_ShaderFileInfo = nullptr;
	}

	void VulkanResourceBindingInstance::BuildResources(class VulkanGraphLocalResourceManager& resourceManager)
	{
		// TODO: Full implementation - register image/buffer/cbuffer resources into local resource manager
		// References D3D12 GPUResourceBindingInstance::BuildResources
	}

	void VulkanResourceBindingInstance::BuildDescriptors(vk::DescriptorPool pool)
	{
		// TODO: Full implementation - allocate descriptor sets and write descriptors from binding elements
		// References D3D12 GPUResourceBindingInstance::BuildDescriptors
		if (p_ShaderFileInfo == nullptr)
			return;

		// Allocate descriptor sets from binding info
		auto& shaderBindingInfo = p_ShaderFileInfo->shaderBindingInfo;
		if (shaderBindingInfo.setLayoutInfos.empty())
			return;

		castl::vector<vk::DescriptorSetLayout> layouts;
		for (auto& setLayoutInfo : shaderBindingInfo.setLayoutInfos)
		{
			castl::vector<vk::DescriptorSetLayoutBinding> vkBindings;
			auto createInfo = setLayoutInfo.GetCreateInfo(vkBindings);
			// TODO: Create or retrieve cached descriptor set layout
		}
	}

	void VulkanResourceBindingInstance::SetUniformBuffer(uint32_t set, uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range)
	{
		m_BufferInfos.push_back({ buffer, offset, range });

		vk::WriteDescriptorSet write{};
		write.dstSet = m_DescriptorSets[set];
		write.dstBinding = binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eUniformBuffer;
		write.pBufferInfo = &m_BufferInfos.back();
		m_PendingWrites.push_back(write);
	}

	void VulkanResourceBindingInstance::SetStorageBuffer(uint32_t set, uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range)
	{
		m_BufferInfos.push_back({ buffer, offset, range });

		vk::WriteDescriptorSet write{};
		write.dstSet = m_DescriptorSets[set];
		write.dstBinding = binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eStorageBuffer;
		write.pBufferInfo = &m_BufferInfos.back();
		m_PendingWrites.push_back(write);
	}

	void VulkanResourceBindingInstance::SetSampledImage(uint32_t set, uint32_t binding, vk::ImageView imageView, vk::ImageLayout layout, vk::Sampler sampler)
	{
		m_ImageInfos.push_back({ sampler, imageView, layout });

		vk::WriteDescriptorSet write{};
		write.dstSet = m_DescriptorSets[set];
		write.dstBinding = binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		write.pImageInfo = &m_ImageInfos.back();
		m_PendingWrites.push_back(write);
	}

	void VulkanResourceBindingInstance::SetStorageImage(uint32_t set, uint32_t binding, vk::ImageView imageView, vk::ImageLayout layout)
	{
		m_ImageInfos.push_back({ {}, imageView, layout });

		vk::WriteDescriptorSet write{};
		write.dstSet = m_DescriptorSets[set];
		write.dstBinding = binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eStorageImage;
		write.pImageInfo = &m_ImageInfos.back();
		m_PendingWrites.push_back(write);
	}

	void VulkanResourceBindingInstance::SetSampler(uint32_t set, uint32_t binding, vk::Sampler sampler)
	{
		m_ImageInfos.push_back({ sampler, {}, {} });

		vk::WriteDescriptorSet write{};
		write.dstSet = m_DescriptorSets[set];
		write.dstBinding = binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eSampler;
		write.pImageInfo = &m_ImageInfos.back();
		m_PendingWrites.push_back(write);
	}

	vk::DescriptorSet VulkanResourceBindingInstance::GetDescriptorSet(uint32_t set) const
	{
		auto it = m_DescriptorSets.find(set);
		if (it != m_DescriptorSets.end())
		{
			return it->second;
		}
		return nullptr;
	}

	bool VulkanResourceBindingInstance::AllocateDescriptorSets(vk::DescriptorPool pool, castl::vector<vk::DescriptorSetLayout> const& layouts)
	{
		if (layouts.empty())
			return true;

		auto device = GetDevice();

		vk::DescriptorSetAllocateInfo allocInfo{};
		allocInfo.descriptorPool = pool;
		allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
		allocInfo.pSetLayouts = layouts.data();

		try
		{
			auto sets = device.allocateDescriptorSets(allocInfo);
			for (uint32_t i = 0; i < sets.size(); ++i)
			{
				m_DescriptorSets[i] = sets[i];
			}
			return true;
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanResourceBindingInstance: Failed to allocate descriptor sets: {}", e.what());
			return false;
		}
	}

	void VulkanResourceBindingInstance::UpdateDescriptorSets()
	{
		if (m_PendingWrites.empty())
			return;

		GetDevice().updateDescriptorSets(m_PendingWrites, {});
		m_PendingWrites.clear();
		m_BufferInfos.clear();
		m_ImageInfos.clear();
	}
}
