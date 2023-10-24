#pragma once
#include "framework.h"
#include <RenderInterface/header/ShaderBindingSet.h>
#include <RenderInterface/header/ShaderBindingBuilder.h>
#include "Containers.h"
#include "VulkanApplicationSubobjectBase.h"
#include "ShaderDescriptorSetAllocator.h"
#include "CVulkanBufferObject.h"
#include "TickUploadingResource.h"
#include "GPUTexture_Impl.h"
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
		virtual void SetConstantSet(std::string const& name, std::shared_ptr<ShaderConstantSet> const& pConstantSet) override;
		virtual void SetTexture(std::string const& name
			, std::shared_ptr<GPUTexture> const& pTexture) override;
		virtual void SetSampler(std::string const& name
			, std::shared_ptr<TextureSampler> const& pSampler) override;
		virtual bool UploadingDone() const override;
		vk::DescriptorSet GetDescriptorSet() const {
			return m_DescriptorSetHandle->GetDescriptorSet();
		}
		virtual void TickUpload() override;
		ShaderBindingSetMetadata const* GetMetadata() const { return p_Metadata; }
	private:
		ShaderBindingSetMetadata const* p_Metadata;
		ShaderDescriptorSetHandle m_DescriptorSetHandle;
		std::unordered_map<std::string, std::shared_ptr<ShaderConstantSet>> m_ConstantSets;
		std::unordered_map<std::string, std::shared_ptr<GPUTexture_Impl>> m_Textures;
		std::unordered_map<std::string, std::shared_ptr<TextureSampler_Impl>> m_Samplers;
	};

	class ShaderBindingSetMetadata
	{
	public:
		void Initialize(ShaderBindingBuilder const& builder);
		ShaderDescriptorSetLayoutInfo const& GetLayoutInfo() const { return m_LayoutInfo; }
		std::unordered_map<std::string, uint32_t> const& GetCBufferNameToBindingIndex() const { return m_CBufferNameToBindingIndex; }
		std::unordered_map<std::string, uint32_t> const& GetTextureNameToBindingIndex() const { return m_TextureNameToBindingIndex; }
		
		uint32_t CBufferNameToBindingIndex(std::string const& cbufferName) const
		{
			auto it = m_CBufferNameToBindingIndex.find(cbufferName);
			if (it == m_CBufferNameToBindingIndex.end())
			{
				return std::numeric_limits<uint32_t>::max();
			}
			return it->second;
		}
		uint32_t TextureNameToBindingIndex(std::string const& textureName) const
		{
			auto it = m_TextureNameToBindingIndex.find(textureName);
			if (it == m_TextureNameToBindingIndex.end())
			{
				return std::numeric_limits<uint32_t>::max();
			}
			return it->second;
		}
		uint32_t SamplerNameToBindingIndex(std::string const& samplerName) const
		{
			auto it = m_SamplerNameToBindingIndex.find(samplerName);
			if (it == m_SamplerNameToBindingIndex.end())
			{
				return std::numeric_limits<uint32_t>::max();
			}
			return it->second;
		}
	private:
		ShaderDescriptorSetLayoutInfo m_LayoutInfo;
		std::unordered_map<std::string, uint32_t> m_CBufferNameToBindingIndex;
		std::unordered_map<std::string, uint32_t> m_TextureNameToBindingIndex;
		std::unordered_map<std::string, uint32_t> m_SamplerNameToBindingIndex;
	};

	class ShaderBindingSetAllocator : public BaseApplicationSubobject
	{
	public:
		ShaderBindingSetAllocator(CVulkanApplication& owner);
		void Create(ShaderBindingBuilder const& builder);
		std::shared_ptr<ShaderBindingSet> AllocateSet();
		virtual void Release() override;
		void TickUploadResources(CTaskGraph* pTaskGraph);
	private:
		ShaderBindingSetMetadata m_Metadata;
		TTickingUpdateResourcePool<ShaderBindingSet_Impl> m_ShaderBindingSetPool;
	};
}