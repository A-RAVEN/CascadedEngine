#pragma once
#include <Platform.h>
#include <ShaderBindingSet.h>
#include <ShaderBindingBuilder.h>
#include <ThreadManager.h>
#include <CASTL/CAUnorderedMap.h>
#include "Containers.h"
#include "VulkanApplicationSubobjectBase.h"
#include "ShaderDescriptorSetAllocator.h"
#include "CVulkanBufferObject.h"
#include "TickUploadingResource.h"
#include "GPUTexture_Impl.h"
#include "GPUBuffer_Impl.h"
#include "TextureSampler_Impl.h"
#include "ShaderConstantSet_Impl.h"

namespace graphics_backend
{
	class ShaderBindingSetMetadata;
	class ShaderDescriptorSetAllocator;
	class ShaderBindingSet_Impl : public BaseTickingUpdateResource, public ShaderBindingSet
	{
	public:
		ShaderBindingSet_Impl(CVulkanApplication& owner);
		void Initialize(ShaderBindingSetMetadata const* inMetaData);
		virtual void SetName(castl::string const& name) override { m_Name = name; }
		virtual void SetConstantSet(castl::string const& name, castl::shared_ptr<ShaderConstantSet> const& pConstantSet) override;
		virtual void SetStructBuffer(castl::string const& name
			, castl::shared_ptr<GPUBuffer> const& pBuffer) override;
		virtual void SetTexture(castl::string const& name
			, castl::shared_ptr<GPUTexture> const& pTexture) override;
		virtual void SetSampler(castl::string const& name
			, castl::shared_ptr<TextureSampler> const& pSampler) override;
		virtual bool UploadingDone() const override;
		virtual ShaderBindingBuilder const& GetBindingSetDesc() const override;
		vk::DescriptorSet GetDescriptorSet() const {
			return m_DescriptorSetHandle->GetDescriptorSet();
		}
		virtual void TickUpload() override;
		ShaderBindingSetMetadata const* GetMetadata() const { return p_Metadata; }
	private:
		ShaderBindingSetMetadata const* p_Metadata;
		ShaderDescriptorSetHandle m_DescriptorSetHandle;
		castl::string m_Name = "";
		castl::unordered_map<castl::string, castl::shared_ptr<ShaderConstantSet>> m_ConstantSets;
		castl::unordered_map<castl::string, castl::shared_ptr<GPUTexture_Impl>> m_Textures;
		castl::unordered_map<castl::string, castl::shared_ptr<TextureSampler_Impl>> m_Samplers;
		castl::unordered_map<castl::string, castl::shared_ptr<GPUBuffer_Impl>> m_StructuredBuffers;
	};

	class ShaderBindingSetMetadata
	{
	public:
		void Initialize(ShaderBindingBuilder const& builder);
		ShaderDescriptorSetLayoutInfo const& GetLayoutInfo() const { return m_LayoutInfo; }
		castl::unordered_map<castl::string, uint32_t> const& GetCBufferNameToBindingIndex() const { return m_CBufferNameToBindingIndex; }
		castl::unordered_map<castl::string, uint32_t> const& GetTextureNameToBindingIndex() const { return m_TextureNameToBindingIndex; }
		castl::unordered_map<castl::string, uint32_t> const& GetSamplerNameToBindingIndex() const { return m_SamplerNameToBindingIndex; }
		castl::unordered_map<castl::string, uint32_t> const& GetStructBufferNameToBindingIndex() const { return m_StructBuffNameToBindingIndex; }

		uint32_t CBufferNameToBindingIndex(castl::string const& cbufferName) const
		{
			auto it = m_CBufferNameToBindingIndex.find(cbufferName);
			if (it == m_CBufferNameToBindingIndex.end())
			{
				return castl::numeric_limits<uint32_t>::max();
			}
			return it->second;
		}
		uint32_t TextureNameToBindingIndex(castl::string const& textureName) const
		{
			auto it = m_TextureNameToBindingIndex.find(textureName);
			if (it == m_TextureNameToBindingIndex.end())
			{
				return castl::numeric_limits<uint32_t>::max();
			}
			return it->second;
		}
		uint32_t SamplerNameToBindingIndex(castl::string const& samplerName) const
		{
			auto it = m_SamplerNameToBindingIndex.find(samplerName);
			if (it == m_SamplerNameToBindingIndex.end())
			{
				return castl::numeric_limits<uint32_t>::max();
			}
			return it->second;
		}
		uint32_t StructBufferNameToBindingIndex(castl::string const& structBufferName) const
		{
			auto it = m_StructBuffNameToBindingIndex.find(structBufferName);
			if (it == m_StructBuffNameToBindingIndex.end())
			{
				return castl::numeric_limits<uint32_t>::max();
			}
			return it->second;
		}
		ShaderBindingBuilder const* GetBindingsDescriptor() const { return p_Builder; }
	private:
		ShaderBindingBuilder const* p_Builder = nullptr;
		ShaderDescriptorSetLayoutInfo m_LayoutInfo;
		castl::unordered_map<castl::string, uint32_t> m_CBufferNameToBindingIndex;
		castl::unordered_map<castl::string, uint32_t> m_TextureNameToBindingIndex;
		castl::unordered_map<castl::string, uint32_t> m_SamplerNameToBindingIndex;
		castl::unordered_map<castl::string, uint32_t> m_StructBuffNameToBindingIndex;
	};

	class ShaderBindingSetAllocator : public VKAppSubObjectBaseNoCopy
	{
	public:
		ShaderBindingSetAllocator(CVulkanApplication& owner);
		void Create(ShaderBindingBuilder const& builder);
		ShaderBindingSetMetadata const& GetMetadata() const { return m_Metadata; }
		castl::shared_ptr<ShaderBindingSet> AllocateSet();
		virtual void Release() override;
		void TickUploadResources(thread_management::CTaskGraph* pTaskGraph);
	private:
		ShaderBindingSetMetadata m_Metadata;
		TTickingUpdateResourcePool<ShaderBindingSet_Impl> m_ShaderBindingSetPool;
	};
}