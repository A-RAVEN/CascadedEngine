#pragma once
#include "ShaderBindingSetData.h"
#include <header/CRenderGraph.h>
#include <header/CNativeRenderPassInfo.h>
#include <unordered_set>
#include <type_traits>

namespace graphics_backend
{
	class IGraphInternalData
	{
	private:
		TIndex m_InternalDataIndex;
	};

	template<typename TDesc>
	class InternalDataManager
	{
	public:

	public:
		std::vector<TDesc> m_DescList;
		std::unordered_map<TDesc, uint32_t, hash_utils::default_hashAlg> m_DescriptorToDataID;
		std::vector<TIndex> m_DataIndexList;
	};

	class CRenderGraph_Impl : public CRenderGraph
	{
	public:
		virtual TextureHandle NewTextureHandle(GPUTextureDescriptor const& textureDesc) override;
		virtual GPUBufferHandle NewGPUBufferHandle(EBufferUsageFlags usageFlags
			, uint64_t count
			, uint64_t stride) override;
		virtual TextureHandle RegisterWindowBackbuffer(std::shared_ptr<WindowHandle> window) override;
		virtual CRenderpassBuilder& NewRenderPass(std::vector<CAttachmentInfo> const& inAttachmentInfo) override;
		virtual ShaderBindingSetHandle NewShaderBindingSetHandle(ShaderBindingBuilder const& builder) override;

		virtual uint32_t GetRenderNodeCount() const override;
		virtual CRenderpassBuilder const& GetRenderPass(uint32_t nodeID) const override;
		virtual TextureHandle TextureHandleByIndex(TIndex index) const override;

		virtual TextureHandleInternalInfo const& GetTextureHandleInternalInfo(TIndex index) const override { return m_TextureHandleIdToInternalInfo[index]; }
		virtual uint32_t GetTextureHandleCount() const override { return m_TextureHandleIdToInternalInfo.size(); }
		virtual uint32_t GetTextureTypesDescriptorCount() const override { return m_TextureDescriptorList.size(); }
		virtual GPUTextureDescriptor const& GetTextureDescriptor(TIndex descriptorIndex) const override { return m_TextureDescriptorList[descriptorIndex]; }

		virtual IShaderBindingSetData* GetBindingSetData(TIndex bindingSetIndex) override
		{
			return &m_ShaderBindingSetDataList[bindingSetIndex];
		}
		virtual IShaderBindingSetData const* GetBindingSetData(TIndex bindingSetIndex) const override
		{
			return &m_ShaderBindingSetDataList[bindingSetIndex];
		}

		virtual uint32_t GetBindingSetDataCount() const override
		{
			return m_ShaderBindingSetDataList.size();
		}

		virtual ShaderBindingBuilder const& GetShaderBindingSetDesc(TIndex descID) const override
		{
			return m_ShaderBindingDescList[descID];
		}

		virtual std::unordered_map<WindowHandle*, TIndex> const& WindowHandleToTextureIndexMap() const override
		{
			return m_RegisteredWindowHandleIDs;
		}

		virtual TIndex WindowHandleToTextureIndex(std::shared_ptr<WindowHandle> handle) const override;
	private:
		TextureHandle NewTextureHandle_Internal(GPUTextureDescriptor const& textureDesc, std::shared_ptr<WindowHandle> window);
	private:
		std::deque<CRenderpassBuilder> m_RenderPasses;

		std::vector<GPUTextureDescriptor> m_TextureDescriptorList;
		std::unordered_map<GPUTextureDescriptor, uint32_t, hash_utils::default_hashAlg> m_DescriptorToDataID;
		std::vector<TextureHandleInternalInfo> m_TextureHandleIdToInternalInfo;
		std::unordered_map<WindowHandle*, TIndex> m_RegisteredWindowHandleIDs;

		std::unordered_map<ShaderBindingBuilder, TIndex, hash_utils::default_hashAlg> m_ShaderBindingDescToIndex;
		std::vector<ShaderBindingBuilder> m_ShaderBindingDescList;
		std::vector<ShaderBindingSetData_Internal> m_ShaderBindingSetDataList;
	};
}