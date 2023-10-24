#include "pch.h"
#include "VulkanApplication.h"
#include "ShaderBindingSet_Impl.h"

namespace graphics_backend 
{
	ShaderBindingSet_Impl::ShaderBindingSet_Impl(CVulkanApplication& owner) : BaseTickingUpdateResource(owner)
	{
	}
	void ShaderBindingSet_Impl::Initialize(ShaderBindingSetMetadata const* inMetaData)
	{
		p_Metadata = inMetaData;
	}
	void ShaderBindingSet_Impl::SetConstantSet(std::string const& name, std::shared_ptr<ShaderConstantSet> const& pConstantSet)
	{
		m_ConstantSets.insert(std::make_pair(name, pConstantSet));
		MarkDirtyThisFrame();
	}

	void ShaderBindingSet_Impl::SetTexture(std::string const& name
		, std::shared_ptr<GPUTexture> const& pTexture)
	{
		std::shared_ptr<GPUTexture_Impl> texture = std::static_pointer_cast<GPUTexture_Impl>(pTexture);
		m_Textures.insert(std::make_pair(name, texture));
		MarkDirtyThisFrame();
	}

	void ShaderBindingSet_Impl::SetSampler(std::string const& name
		, std::shared_ptr<TextureSampler> const& pSampler)
	{
		std::shared_ptr<TextureSampler_Impl> sampler = std::static_pointer_cast<TextureSampler_Impl>(pSampler);
		m_Samplers.insert(std::make_pair(name, sampler));
		MarkDirtyThisFrame();
	}

	bool ShaderBindingSet_Impl::UploadingDone() const
	{
		return BaseTickingUpdateResource::UploadingDone();
	}
	void ShaderBindingSet_Impl::TickUpload()
	{
		m_DescriptorSetHandle.RAIIRelease();
		CA_ASSERT(!m_DescriptorSetHandle.IsRAIIAquired(), "RAIIObject Should Be Released Here");
		//{
			auto& descPoolCache = GetVulkanApplication().GetGPUObjectManager().GetShaderDescriptorPoolCache();
			ShaderDescriptorSetLayoutInfo layoutInfo = p_Metadata->GetLayoutInfo();
			auto allocator = descPoolCache.GetOrCreate(layoutInfo).lock();
			m_DescriptorSetHandle = std::move(allocator->AllocateSet());
		//}
		CA_ASSERT(m_DescriptorSetHandle.IsRAIIAquired(), "Descriptor Set Is Not Aquired!");
		vk::DescriptorSet targetSet = m_DescriptorSetHandle->GetDescriptorSet();
		uint32_t writeCount = m_ConstantSets.size() + m_Textures.size() + m_Samplers.size();
		std::vector<vk::WriteDescriptorSet> descriptorWrites;
		if(writeCount > 0)
		{
			descriptorWrites.reserve(writeCount);
			auto& nameToIndex = p_Metadata->GetCBufferNameToBindingIndex();
			for (auto pair : m_ConstantSets)
			{
				auto& name = pair.first;
				auto set = std::static_pointer_cast<ShaderConstantSet_Impl>(pair.second);
				uint32_t bindingIndex = p_Metadata->CBufferNameToBindingIndex(name);
				if (bindingIndex != std::numeric_limits<uint32_t>::max())
				{
					vk::DescriptorBufferInfo bufferInfo{set->GetBufferObject()->GetBuffer(), 0, VK_WHOLE_SIZE};
					vk::WriteDescriptorSet writeSet{targetSet
						, bindingIndex
						, 0
						, 1
						, vk::DescriptorType::eUniformBuffer
						, nullptr
						, & bufferInfo
						, nullptr
					};
					descriptorWrites.push_back(writeSet);
				}
			}

			for (auto pair : m_Textures)
			{
				auto& name = pair.first;
				auto& texture = pair.second;
				uint32_t bindingIndex = p_Metadata->TextureNameToBindingIndex(name);
				if (bindingIndex != std::numeric_limits<uint32_t>::max())
				{
					if (!texture->UploadingDone())
						return;
					vk::DescriptorImageInfo imageInfo{ {}
						, texture->GetDefaultImageView()
						, vk::ImageLayout::eShaderReadOnlyOptimal };

					vk::WriteDescriptorSet writeSet{targetSet
						, bindingIndex
						, 0
						, 1
						, vk::DescriptorType::eSampledImage
						, & imageInfo
						, nullptr
						, nullptr
					};
					descriptorWrites.push_back(writeSet);
				}
			}

			for (auto pair : m_Samplers)
			{
				auto& name = pair.first;
				auto& sampler = pair.second;
				uint32_t bindingIndex = p_Metadata->SamplerNameToBindingIndex(name);
				if (bindingIndex != std::numeric_limits<uint32_t>::max())
				{
					vk::DescriptorImageInfo samplerInfo{ sampler->GetSampler()
						, {}
						, vk::ImageLayout::eShaderReadOnlyOptimal };

					vk::WriteDescriptorSet writeSet{targetSet
						, bindingIndex
						, 0
						, 1
						, vk::DescriptorType::eSampler
						, & samplerInfo
						, nullptr
						, nullptr
					};
					descriptorWrites.push_back(writeSet);
				}
			}
		}
		if (descriptorWrites.size() > 0)
		{
			GetDevice().updateDescriptorSets(descriptorWrites, {});
		}
		MarkUploadingDoneThisFrame();
	}
	void ShaderBindingSetMetadata::Initialize(ShaderBindingBuilder const& builder)
	{
		auto& constantBufferDescriptors = builder.GetConstantBufferDescriptors();
		auto& textureDescriptors = builder.GetTextureDescriptors();
		auto& samplerDescriptors = builder.GetTextureSamplers();
		m_LayoutInfo = ShaderDescriptorSetLayoutInfo{ builder };

		uint32_t bindingIndex = 0;
		for (auto& desc : constantBufferDescriptors)
		{
			m_CBufferNameToBindingIndex.emplace(desc.GetName(), bindingIndex++);
		};

		for (auto& desc : textureDescriptors)
		{
			m_TextureNameToBindingIndex.emplace(desc.first, bindingIndex++);
		}

		for (auto& sampler : samplerDescriptors)
		{
			m_SamplerNameToBindingIndex.emplace(sampler, bindingIndex++);
		}
	}
	ShaderBindingSetAllocator::ShaderBindingSetAllocator(CVulkanApplication& owner) : BaseApplicationSubobject(owner)
		, m_ShaderBindingSetPool(owner)
	{
	}
	void ShaderBindingSetAllocator::Create(ShaderBindingBuilder const& builder)
	{
		m_Metadata.Initialize(builder);
	}
	std::shared_ptr<ShaderBindingSet> ShaderBindingSetAllocator::AllocateSet()
	{
		return m_ShaderBindingSetPool.AllocShared(&m_Metadata);
	}
	void ShaderBindingSetAllocator::Release()
	{
		m_ShaderBindingSetPool.ReleaseAll();
	}
	void ShaderBindingSetAllocator::TickUploadResources(CTaskGraph* pTaskGraph)
	{
		m_ShaderBindingSetPool.TickUpload(pTaskGraph);
	}
}
