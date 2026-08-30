#include <GPUGraph/VulkanResourceBindingInstance.h>
#include <GPUGraph/VulkanGraphExecutor.h>
#include <Utils/VulkanDebug.h>
#include <string>
#include <GPUGraph/VulkanConstantBufferManager.h>
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

	static vk::ShaderStageFlags ToShaderStageFlags(EShaderTypeFlags flags)
	{
		vk::ShaderStageFlags result{};
		IterateShaderTypeFlags(flags, [&result](EShaderTypeMask mask)
		{
			switch (mask)
			{
			case EShaderTypeMask::eVert: result |= vk::ShaderStageFlagBits::eVertex; break;
			case EShaderTypeMask::eFrag: result |= vk::ShaderStageFlagBits::eFragment; break;
			case EShaderTypeMask::eComp: result |= vk::ShaderStageFlagBits::eCompute; break;
			case EShaderTypeMask::eTessCtr: result |= vk::ShaderStageFlagBits::eTessellationControl; break;
			case EShaderTypeMask::eTessEvl: result |= vk::ShaderStageFlagBits::eTessellationEvaluation; break;
			case EShaderTypeMask::eGeom: result |= vk::ShaderStageFlagBits::eGeometry; break;
			default: break;
			}
		});
		return result;
	}

	void VulkanResourceBindingInstance::BuildResources(VulkanGraphLocalResourceManager& resourceManager, VulkanConstantBufferManager& cbufferManager, GPUGraph const& graph)
	{
		if (p_ShaderFileInfo == nullptr)
			return;

		auto& shaderBindingInfo = p_ShaderFileInfo->shaderBindingInfo;

		// 3.2: Lookup CBuffer resource IDs (pre-registered in RegisterCBufferForAliasing)
		for (auto& cbuffer : m_CBufferBindings)
		{
			if (cbuffer.pCBufferStruct == nullptr)
				continue;

			cbuffer.gpuBufferResourceId = cbufferManager.GetResourceId(cbuffer.pCBufferStruct);
			cbuffer.usingStages = ToShaderStageFlags(p_ShaderFileInfo->GetShaderStageUsage(cbuffer.bindingInfo.usageMask));
		}

		// 3.3: Resolve image bindings. After D-A/D-B/D-C/D1 every shader-bound graph (Internal)
		// resource is pre-registered AND physically bound during AllocateAliasedResources (Phase
		// 4.5), so GetTextureView already returns non-null. The old unconditional fallback —
		// RegisterTemporaryTexture + RegisterTextureHandle — ran in Phase 5 and created a resource
		// born at batch 0 AFTER aliasing planning, so it was never bound; the descriptor write still
		// skipped (08114). D2: make it inert — do NOT create a Phase-5 dead resource. External
		// handles resolve via GetTextureView's external fallback (non-null → continue), so the only
		// way to reach here is a genuine pre-registration gap → record + explicitly skip.
		for (auto& image : m_ImageBindings)
		{
			image.usingStages = ToShaderStageFlags(p_ShaderFileInfo->GetShaderStageUsage(image.bindingInfo.usageMask));
			for (auto& binding : image.bindings)
			{
				auto const& imageHandle = binding.first;
				if (!imageHandle.IsValid())
					continue;
				if (resourceManager.GetTextureView(imageHandle))
					continue;

				CA_LOG_WARN("VulkanResourceBindingInstance: image handle not bound; skipping descriptor "
					"(expected pre-bound by D1/D-A/D-B/D-C in Phase 4.5)");
			}
		}

		// 3.4: Resolve buffer bindings — same D2 rationale as images: after the Phase 4.5 binding
		// every shader-bound graph buffer is bound, so GetBuffer returns non-null. The old fallback
		// registered a Phase-5 batch-0 resource that was never bound → 08114. Make it inert.
		for (auto& buffer : m_BufferBindings)
		{
			buffer.usingStages = ToShaderStageFlags(p_ShaderFileInfo->GetShaderStageUsage(buffer.bindingInfo.usageMask));
			for (auto& bufferHandle : buffer.bindings)
			{
				if (!bufferHandle.IsValid())
					continue;
				if (resourceManager.GetBuffer(bufferHandle))
					continue;

				CA_LOG_WARN("VulkanResourceBindingInstance: buffer handle not bound; skipping descriptor "
					"(expected pre-bound by D1/D-A/D-B/D-C in Phase 4.5)");
			}
		}

		// 3.5: Set usingStages for sampler bindings
		for (auto& sampler : m_SamplerBindings)
		{
			sampler.usingStages = ToShaderStageFlags(p_ShaderFileInfo->GetShaderStageUsage(sampler.bindingInfo.usageMask));
		}
	}

	void VulkanResourceBindingInstance::BuildDescriptors(VulkanGraphLocalResourceManager& resourceManager, vk::DescriptorPool pool)
	{
		// NOTE: Every frame, VulkanFrameContext::Aquire() calls vkResetDescriptorPool,
		// which returns ALL descriptor sets in the pool to the initial (blank) state.
		// Consequently, every vkDescriptorSet allocated from this pool is empty and
		// requires full vkUpdateDescriptorSets rewrite — even if the underlying buffer/
		// image/sampler handles are unchanged from the previous frame.
		//
		// TODO: future work — descriptor write caching can be implemented once the
		// descriptor pool lifecycle is restructured to support cross-frame set reuse.
		// See design.md D1 for three candidate strategies (FREE_DESCRIPTOR_SET_BIT +
		// vkFreeDescriptorSets, cross-frame set reuse, or multi-pool rotation).
		if (p_ShaderFileInfo == nullptr)
			return;

		auto& shaderBindingInfo = p_ShaderFileInfo->shaderBindingInfo;
		if (shaderBindingInfo.setLayoutInfos.empty())
			return;

		m_DescriptorSets.clear();
		m_PendingWrites.clear();
		m_BufferInfos.clear();
		m_ImageInfos.clear();
		// F42 audit fix: reserve by TOTAL ELEMENT count, not binding count — the
		// per-element write loops below push image.bindings.size() /
		// samplerDescriptors.size() / bindings.size() entries per binding. Reserving too
		// little lets the vector reallocate mid-write, dangling the &back() pointers
		// stored in pending writes (memory bug).
		{
			size_t bufferInfoCount = m_CBufferBindings.size();
			for (auto const& b : m_BufferBindings) bufferInfoCount += b.bindings.size();
			size_t imageInfoCount = 0;
			for (auto const& img : m_ImageBindings) imageInfoCount += img.bindings.size();
			for (auto const& smp : m_SamplerBindings) imageInfoCount += smp.samplerDescriptors.size();
			m_BufferInfos.reserve(bufferInfoCount);
			m_ImageInfos.reserve(imageInfoCount);
		}

		// 5.2: Allocate descriptor sets from cached layouts
		castl::vector<castl::pair<uint32_t, vk::DescriptorSetLayout>> setLayoutPairs;
		for (auto& setLayoutInfo : shaderBindingInfo.setLayoutInfos)
		{
			vk::DescriptorSetLayout layout = GetApp()->GetOrCreateDescriptorSetLayout(setLayoutInfo);
			if (!layout)
			{
				CA_LOG_ERR("Failed to get or create descriptor set layout for set {}", setLayoutInfo.setIndex);
				continue;
			}
			setLayoutPairs.push_back({ setLayoutInfo.setIndex, layout });
		}

		if (setLayoutPairs.empty())
			return;

		// Sort by setIndex to ensure correct ordering for allocation
		castl::sort(setLayoutPairs.begin(), setLayoutPairs.end(),
			[](auto const& a, auto const& b) { return a.first < b.first; });

		if (!AllocateDescriptorSets(pool, setLayoutPairs))
			return;

		// 5.3: Write CBuffer descriptors
		for (auto& cbuffer : m_CBufferBindings)
		{
			if (cbuffer.gpuBufferResourceId == 0)
				continue;

			vk::Buffer buffer = resourceManager.GetBuffer(cbuffer.gpuBufferResourceId);
			if (!buffer)
				continue;

			uint64_t range = cbuffer.pCBufferStruct ? cbuffer.pCBufferStruct->GetCBufferSize() : 256;
			SetUniformBuffer(cbuffer.bindingInfo.spaceID, cbuffer.bindingInfo.bindingID, buffer, 0, range);
		}

		// 5.4: Write Image descriptors
		for (auto& image : m_ImageBindings)
		{
			if (image.bindings.empty())
				continue;

			bool isUAV = (image.resourceUsages == EResourceUsage::eShaderUnorderedAccess);

			// F42: write EVERY array element — the layout declares descriptorCount =
			// bindings.size(); writing only bindings[0] would mismatch it.
			for (uint32_t elemIdx = 0; elemIdx < image.bindings.size(); ++elemIdx)
			{
				vk::ImageView imageView = resourceManager.GetTextureView(image.bindings[elemIdx].first);
				if (!imageView)
					continue;

				// R4-5: the descriptor's imageLayout MUST match the image's ACTUAL layout
				// at sampling time (VUID-VkDescriptorImageInfo-imageLayout-00344). The
				// barrier/layout-tracking chain transitions sampled images to
				// SHADER_READ_ONLY_OPTIMAL everywhere (ComputeAccessToImageLayout,
				// transfer-pass, finalize pass) — so the descriptor uses the same layout
				// for all formats. Depth views are single-aspect (GetImageViewAspect /
				// graph-local GetImageAspectMask) per VUID-VkDescriptorImageInfo-imageView-01976.
				vk::ImageLayout layout = isUAV
					? vk::ImageLayout::eGeneral
					: vk::ImageLayout::eShaderReadOnlyOptimal;

				if (isUAV)
				{
					SetStorageImage(image.bindingInfo.spaceID, image.bindingInfo.bindingID, imageView, layout, elemIdx);
				}
				else
				{
					if (shaderBindingInfo.samplerInfos.empty())
						CA_LOG_WARN("SampledImage bound without corresponding SamplerBinding - configure sampler for correct behavior");
					SetSampledImage(image.bindingInfo.spaceID, image.bindingInfo.bindingID, imageView, layout, elemIdx);
				}
			}
		}

		// 5.5: Write Buffer descriptors
		for (auto& buffer : m_BufferBindings)
		{
			if (buffer.bindings.empty())
				continue;

			// F42: write every array element (same descriptorCount consistency as images).
			for (uint32_t elemIdx = 0; elemIdx < buffer.bindings.size(); ++elemIdx)
			{
				vk::Buffer vkBuffer = resourceManager.GetBuffer(buffer.bindings[elemIdx]);
				if (!vkBuffer)
					continue;

				SetStorageBuffer(buffer.bindingInfo.spaceID, buffer.bindingInfo.bindingID, vkBuffer, 0, VK_WHOLE_SIZE, elemIdx);
			}
		}

		// 5.6: Write Sampler descriptors
		for (auto& sampler : m_SamplerBindings)
		{
			if (sampler.samplerDescriptors.empty())
				continue;

			// F42: write every array element (same descriptorCount consistency as images).
			for (uint32_t elemIdx = 0; elemIdx < sampler.samplerDescriptors.size(); ++elemIdx)
			{
				auto const& samplerDesc = sampler.samplerDescriptors[elemIdx];
				vk::Sampler vkSampler = GetApp()->GetSamplerManager().GetOrCreateSampler(samplerDesc);
				SetSampler(sampler.bindingInfo.spaceID, sampler.bindingInfo.bindingID, vkSampler, elemIdx);
			}
		}

		// 5.7: Submit all pending writes
		UpdateDescriptorSets();
	}

	void VulkanResourceBindingInstance::SetUniformBuffer(uint32_t set, uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t arrayElement)
	{
		auto it = m_DescriptorSets.find(set);
		CA_ASSERT_BREAK(it != m_DescriptorSets.end(), "DescriptorSet not allocated for set {}", set);

		m_BufferInfos.push_back({ buffer, offset, range });

		vk::WriteDescriptorSet write{};
		write.dstSet = it->second;
		write.dstBinding = binding;
		write.dstArrayElement = arrayElement;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eUniformBuffer;
		write.pBufferInfo = &m_BufferInfos.back();
		m_PendingWrites.push_back(write);
	}

	void VulkanResourceBindingInstance::SetStorageBuffer(uint32_t set, uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t arrayElement)
	{
		auto it = m_DescriptorSets.find(set);
		CA_ASSERT_BREAK(it != m_DescriptorSets.end(), "DescriptorSet not allocated for set {}", set);

		m_BufferInfos.push_back({ buffer, offset, range });

		vk::WriteDescriptorSet write{};
		write.dstSet = it->second;
		write.dstBinding = binding;
		write.dstArrayElement = arrayElement;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eStorageBuffer;
		write.pBufferInfo = &m_BufferInfos.back();
		m_PendingWrites.push_back(write);
	}

	void VulkanResourceBindingInstance::SetSampledImage(uint32_t set, uint32_t binding, vk::ImageView imageView, vk::ImageLayout layout, uint32_t arrayElement)
	{
		auto it = m_DescriptorSets.find(set);
		CA_ASSERT_BREAK(it != m_DescriptorSets.end(), "DescriptorSet not allocated for set {}", set);

		m_ImageInfos.push_back({ {}, imageView, layout });

		vk::WriteDescriptorSet write{};
		write.dstSet = it->second;
		write.dstBinding = binding;
		write.dstArrayElement = arrayElement;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eSampledImage;
		write.pImageInfo = &m_ImageInfos.back();
		m_PendingWrites.push_back(write);
	}

	void VulkanResourceBindingInstance::SetStorageImage(uint32_t set, uint32_t binding, vk::ImageView imageView, vk::ImageLayout layout, uint32_t arrayElement)
	{
		auto it = m_DescriptorSets.find(set);
		CA_ASSERT_BREAK(it != m_DescriptorSets.end(), "DescriptorSet not allocated for set {}", set);

		m_ImageInfos.push_back({ {}, imageView, layout });

		vk::WriteDescriptorSet write{};
		write.dstSet = it->second;
		write.dstBinding = binding;
		write.dstArrayElement = arrayElement;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eStorageImage;
		write.pImageInfo = &m_ImageInfos.back();
		m_PendingWrites.push_back(write);
	}

	void VulkanResourceBindingInstance::SetSampler(uint32_t set, uint32_t binding, vk::Sampler sampler, uint32_t arrayElement)
	{
		auto it = m_DescriptorSets.find(set);
		CA_ASSERT_BREAK(it != m_DescriptorSets.end(), "DescriptorSet not allocated for set {}", set);

		m_ImageInfos.push_back({ sampler, {}, {} });

		vk::WriteDescriptorSet write{};
		write.dstSet = it->second;
		write.dstBinding = binding;
		write.dstArrayElement = arrayElement;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eSampler;
		write.pImageInfo = &m_ImageInfos.back();
		m_PendingWrites.push_back(write);
	}

	castl::vector<castl::pair<uint32_t, vk::DescriptorSet>> VulkanResourceBindingInstance::GetDescriptorSetsSorted() const
	{
		castl::vector<castl::pair<uint32_t, vk::DescriptorSet>> result;
		result.reserve(m_DescriptorSets.size());
		for (auto& [setIndex, set] : m_DescriptorSets)
		{
			if (set)
			{
				result.push_back({ setIndex, set });
			}
		}
		castl::sort(result.begin(), result.end(),
			[](auto const& a, auto const& b) { return a.first < b.first; });
		return result;
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

	bool VulkanResourceBindingInstance::AllocateDescriptorSets(vk::DescriptorPool pool, castl::vector<castl::pair<uint32_t, vk::DescriptorSetLayout>> const& setLayoutPairs)
	{
		if (setLayoutPairs.empty())
			return true;

		auto device = GetDevice();

		castl::vector<vk::DescriptorSetLayout> layouts;
		layouts.reserve(setLayoutPairs.size());
		for (auto const& pair : setLayoutPairs)
		{
			layouts.push_back(pair.second);
		}

		vk::DescriptorSetAllocateInfo allocInfo{};
		allocInfo.descriptorPool = pool;
		allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
		allocInfo.pSetLayouts = layouts.data();

		try
		{
			auto sets = device.allocateDescriptorSets(allocInfo);
			for (uint32_t i = 0; i < sets.size(); ++i)
			{
				m_DescriptorSets[setLayoutPairs[i].first] = sets[i];
#ifndef NDEBUG
				std::string dsName = "DescSet:set" + std::to_string(setLayoutPairs[i].first);
				SetVKObjectDebugName(device, sets[i], dsName.c_str());
#endif
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
