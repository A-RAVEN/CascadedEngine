#include "pch.h"
#include "VulkanApplication.h"
#include "ShaderBindingSet_Impl.h"
#include "VulkanDebug.h"

namespace graphics_backend 
{
	using namespace thread_management;

	ShaderBindingSet_Impl::ShaderBindingSet_Impl(CVulkanApplication& owner) : BaseTickingUpdateResource(owner)
	{
	}
	void ShaderBindingSet_Impl::Initialize(ShaderBindingSetMetadata const* inMetaData)
	{
		p_Metadata = inMetaData;
	}
	void ShaderBindingSet_Impl::SetConstantSet(castl::string const& name, castl::shared_ptr<ShaderConstantSet> const& pConstantSet)
	{
		m_ConstantSets.insert(castl::make_pair(name, pConstantSet));
		MarkDirtyThisFrame();
	}

	void ShaderBindingSet_Impl::SetStructBuffer(castl::string const& name
		, castl::shared_ptr<GPUBuffer> const& pBuffer)
	{
		castl::shared_ptr<GPUBuffer_Impl> buffer = castl::static_pointer_cast<GPUBuffer_Impl>(pBuffer);
		m_StructuredBuffers.insert(castl::make_pair(name, buffer));
		MarkDirtyThisFrame();
	}

	void ShaderBindingSet_Impl::SetTexture(castl::string const& name
		, castl::shared_ptr<GPUTexture> const& pTexture)
	{
		castl::shared_ptr<GPUTexture_Impl> texture = castl::static_pointer_cast<GPUTexture_Impl>(pTexture);
		m_Textures.insert(castl::make_pair(name, texture));
		MarkDirtyThisFrame();
	}

	void ShaderBindingSet_Impl::SetSampler(castl::string const& name
		, castl::shared_ptr<TextureSampler> const& pSampler)
	{
		castl::shared_ptr<TextureSampler_Impl> sampler = castl::static_pointer_cast<TextureSampler_Impl>(pSampler);
		m_Samplers.insert(castl::make_pair(name, sampler));
		MarkDirtyThisFrame();
	}

	bool ShaderBindingSet_Impl::UploadingDone() const
	{
		return BaseTickingUpdateResource::UploadingDone();
	}
	ShaderBindingBuilder const& ShaderBindingSet_Impl::GetBindingSetDesc() const
	{
		return *p_Metadata->GetBindingsDescriptor();
	}
	void ShaderBindingSet_Impl::TickUpload()
	{
		m_DescriptorSetHandle.RAIIRelease();
		CA_ASSERT(!m_DescriptorSetHandle.IsRAIIAquired(), "RAIIObject Should Be Released Here");
		//{
			auto& descPoolCache = GetVulkanApplication().GetGPUObjectManager().GetShaderDescriptorPoolCache();
			ShaderDescriptorSetLayoutInfo layoutInfo = p_Metadata->GetLayoutInfo();
			auto allocator = descPoolCache.GetOrCreate(layoutInfo);
			m_DescriptorSetHandle = castl::move(allocator->AllocateSet());
			vulkan_backend::SetVKObjectDebugName(GetDevice(), m_DescriptorSetHandle.Get().GetDescriptorSet(), m_Name.c_str());
		//}
		CA_ASSERT(m_DescriptorSetHandle.IsRAIIAquired(), "Descriptor Set Is Not Aquired!");
		vk::DescriptorSet targetSet = m_DescriptorSetHandle->GetDescriptorSet();
		uint32_t writeCount = m_ConstantSets.size() + m_Textures.size() + m_Samplers.size();

		castl::vector<vk::DescriptorImageInfo> imageInfoList;
		castl::vector<vk::DescriptorBufferInfo> bufferInfoList;

		auto newBufferInfo = [&bufferInfoList](vk::DescriptorBufferInfo const& bufferInfo) -> vk::DescriptorBufferInfo*
			{
				bufferInfoList.push_back(bufferInfo);
				return &(*(bufferInfoList.rbegin()));
			};
		auto newImageInfo = [&imageInfoList](vk::DescriptorImageInfo const& imgInfo) -> vk::DescriptorImageInfo*
			{
				imageInfoList.push_back(imgInfo);
				return &(*(imageInfoList.rbegin()));
			};
		castl::vector<vk::WriteDescriptorSet> descriptorWrites;
		if(writeCount > 0)
		{
			imageInfoList.reserve(m_Textures.size() + m_Samplers.size());
			bufferInfoList.reserve(m_ConstantSets.size() + m_StructuredBuffers.size());
			descriptorWrites.reserve(writeCount);
			auto& nameToIndex = p_Metadata->GetCBufferNameToBindingIndex();
			for (auto pair : m_ConstantSets)
			{
				auto& name = pair.first;
				auto set = castl::static_pointer_cast<ShaderConstantSet_Impl>(pair.second);
				uint32_t bindingIndex = p_Metadata->CBufferNameToBindingIndex(name);
				if (bindingIndex != castl::numeric_limits<uint32_t>::max())
				{
					vk::DescriptorBufferInfo* bufferInfo = newBufferInfo({set->GetBufferObject()->GetBuffer(), 0, VK_WHOLE_SIZE});
					vk::WriteDescriptorSet writeSet
					{
						targetSet
						, bindingIndex
						, 0
						, 1
						, vk::DescriptorType::eUniformBuffer
						, nullptr
						, bufferInfo
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
				if (bindingIndex != castl::numeric_limits<uint32_t>::max())
				{
					if (!texture->UploadingDone())
					{
						CA_LOG_ERR("Texture Uploading Not Done " + m_Name);
						return;
					}
					CA_ASSERT(texture->GetDefaultImageView() != vk::ImageView{ nullptr }, "Null Image View");
					vk::DescriptorImageInfo* imageInfo = newImageInfo({ {}
						, texture->GetDefaultImageView()
						, vk::ImageLayout::eShaderReadOnlyOptimal });

					vk::WriteDescriptorSet writeSet{targetSet
						, bindingIndex
						, 0
						, 1
						, vk::DescriptorType::eSampledImage
						, imageInfo
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
				if (bindingIndex != castl::numeric_limits<uint32_t>::max())
				{
					vk::DescriptorImageInfo* samplerInfo = newImageInfo({ sampler->GetSampler()
						, {}
						, vk::ImageLayout::eShaderReadOnlyOptimal });

					vk::WriteDescriptorSet writeSet{targetSet
						, bindingIndex
						, 0
						, 1
						, vk::DescriptorType::eSampler
						, samplerInfo
						, nullptr
						, nullptr
					};
					descriptorWrites.push_back(writeSet);
				}
			}

			for (auto pair : m_StructuredBuffers)
			{
				auto& name = pair.first;
				auto& buffer = pair.second;
				uint32_t bindingIndex = p_Metadata->StructBufferNameToBindingIndex(name);
				if (bindingIndex != castl::numeric_limits<uint32_t>::max())
				{
					vk::DescriptorBufferInfo* bufferInfo = newBufferInfo({buffer->GetVulkanBufferObject()->GetBuffer(), 0, VK_WHOLE_SIZE});
					vk::WriteDescriptorSet writeSet{targetSet
						, bindingIndex
						, 0
						, 1
						, vk::DescriptorType::eStorageBuffer
						, nullptr
						, bufferInfo
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
		p_Builder = &builder;
		auto& constantBufferDescriptors = builder.GetConstantBufferDescriptors();
		auto& textureDescriptors = builder.GetTextureDescriptors();
		auto& samplerDescriptors = builder.GetTextureSamplers();
		auto& strucBuffs = builder.GetStructuredBuffers();
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

		for (auto& structBuf : strucBuffs)
		{
			m_StructBuffNameToBindingIndex.emplace(structBuf, bindingIndex++);
		}
	}
	ShaderBindingSetAllocator::ShaderBindingSetAllocator(CVulkanApplication& owner) : VKAppSubObjectBaseNoCopy(owner)
		, m_ShaderBindingSetPool(owner)
	{
	}
	void ShaderBindingSetAllocator::Create(ShaderBindingBuilder const& builder)
	{
		m_Metadata.Initialize(builder);
	}
	castl::shared_ptr<ShaderBindingSet> ShaderBindingSetAllocator::AllocateSet()
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
