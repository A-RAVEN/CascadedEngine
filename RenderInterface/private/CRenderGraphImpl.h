#pragma once
#include "ShaderBindingSetData.h"
#include <header/CRenderGraph.h>
#include <header/CNativeRenderPassInfo.h>
#include <unordered_set>
#include <type_traits>
#include <header/GPUBuffer.h>
#include "GPUBufferData.h"

namespace graphics_backend
{
	template<typename TDesc>
	class InternalDataManager
	{
	public:
		TIndex RegisterNewData(TDesc const& inDesc, uint32_t& outDescID)
		{
			auto found = m_DescToDescID.find(inDesc);
			if (found == m_DescToDescID.end())
			{
				outDescID = m_DescList.size();
				m_DescList.push_back(inDesc);
				m_DescToDescID.insert(std::make_pair(inDesc, outDescID));
			}
			else
			{
				outDescID = found->second;
			}
			TIndex result = m_DataIndexToDescID.size();
			m_DataIndexToDescID.push_back(outDescID);
			return result;
		}

		TDesc const& DescIDToDesc(uint32_t descID) const
		{
			return m_DescList[descID];
		}

		TDesc const& DataIDToDesc(TIndex dataID) const
		{
			return m_DescList[m_DataIndexToDescID[dataID]];
		}

		uint32_t DescToDescID(TDesc const& inDesc)
		{
			auto found = m_DescToDescID.find(inDesc);
			if (found != m_DescToDescID.end())
			{
				return found.second;
			}
			return INVALID_INDEX;
		}

		void Clear(bool clearDescs = false)
		{
			m_DataIndexToDescID.clear();
			if (clearDescs)
			{
				m_DescToDescID.clear();
				m_DescList.clear();
			}
		}

		uint32_t DescCount() const
		{
			return m_DescList.size();
		}

		TIndex DataIndexToDescID(TIndex dataID) const
		{
			return m_DataIndexToDescID[dataID];
		}

		uint32_t DataIDCount() const
		{
			return m_DataIndexToDescID.size();
		}
	public:
		//DescID to Desc
		std::vector<TDesc> m_DescList;
		//Desc to DescID
		std::unordered_map<TDesc, uint32_t, hash_utils::default_hashAlg> m_DescToDescID;
		//DataID to DescID
		std::vector<TIndex> m_DataIndexToDescID;
	};

	class CRenderGraph_Impl : public CRenderGraph
	{
	public:
		virtual TextureHandle NewTextureHandle(GPUTextureDescriptor const& textureDesc) override;

		virtual GPUBufferHandle NewGPUBufferHandle(EBufferUsageFlags usageFlags
			, uint64_t count
			, uint64_t stride) override;
		virtual void ScheduleBufferData(GPUBufferHandle bufferHandle, uint64_t bufferOffset, uint64_t dataSize, void* pData) override;
		virtual IGPUBufferInternalData const& GetGPUBufferInternalData(GPUBufferHandle const& bufferHandle) const override;
		virtual GPUBufferDescriptor const& GetGPUBufferDescriptor(GPUBufferHandle const& bufferHandle) const override;

		virtual TextureHandle RegisterWindowBackbuffer(std::shared_ptr<WindowHandle> window) override;
		virtual CRenderpassBuilder& NewRenderPass(std::vector<CAttachmentInfo> const& inAttachmentInfo) override;
		virtual ShaderBindingSetHandle NewShaderBindingSetHandle(ShaderBindingBuilder const& builder) override;
		virtual GPUTextureDescriptor const& GetTextureDescriptor(TextureHandle const& handle) const override;

		virtual uint32_t GetRenderNodeCount() const override;
		virtual CRenderpassBuilder const& GetRenderPass(uint32_t nodeID) const override;
		virtual TextureHandle TextureHandleByIndex(TIndex index) const override;

		virtual TextureHandleInternalInfo const& GetTextureHandleInternalInfo(TIndex index) const override { return m_TextureHandleIdToInternalInfo[index]; }
		virtual uint32_t GetTextureHandleCount() const override { return m_TextureHandleIdToInternalInfo.size(); }
		virtual uint32_t GetTextureTypesDescriptorCount() const override { return m_TextureDescriptorIDPool.DescCount(); }
		virtual GPUTextureDescriptor const& GetTextureDescriptor(TIndex descID) const override { return m_TextureDescriptorIDPool.DescIDToDesc(descID); }

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
			return m_BindingDescriptorIDPool.DescIDToDesc(descID);
		}

		virtual std::unordered_map<WindowHandle*, TIndex> const& WindowHandleToTextureIndexMap() const override
		{
			return m_RegisteredWindowHandleIDs;
		}

		virtual TIndex WindowHandleToTextureIndex(std::shared_ptr<WindowHandle> handle) const override;

		virtual uint32_t GetGPUBufferHandleCount() const override {
			return m_GPUBufferDescriptorIDPool.DataIDCount();
		}

	private:
		TextureHandle NewTextureHandle_Internal(GPUTextureDescriptor const& textureDesc, std::shared_ptr<WindowHandle> window);
	private:
		std::deque<CRenderpassBuilder> m_RenderPasses;

		std::vector<TextureHandleInternalInfo> m_TextureHandleIdToInternalInfo;
		std::unordered_map<WindowHandle*, TIndex> m_RegisteredWindowHandleIDs;
		InternalDataManager<GPUTextureDescriptor> m_TextureDescriptorIDPool;

		std::vector<GPUBufferData_Internal> m_GPUBufferInternalInfo;
		InternalDataManager<GPUBufferDescriptor> m_GPUBufferDescriptorIDPool;

		std::vector<ShaderBindingSetData_Internal> m_ShaderBindingSetDataList;
		InternalDataManager<ShaderBindingBuilder> m_BindingDescriptorIDPool;
	};
}