#pragma once
#include "ShaderBindingSetData.h"
#include "ShaderConstantSetData.h"
#include <CRenderGraph.h>
#include <CNativeRenderPassInfo.h>
#include <GPUBuffer.h>
#include "GPUBufferData.h"
#include "TextureInternalData.h"
#include "ShaderConstantSetDataLayout.h"

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
				m_DescToDescID.insert(castl::make_pair(inDesc, outDescID));
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
		castl::vector<TDesc> m_DescList;
		//Desc to DescID
		castl::unordered_map<TDesc, uint32_t, hash_utils::default_hashAlg> m_DescToDescID;
		//DataID to DescID
		castl::vector<TIndex> m_DataIndexToDescID;
	};

	class CRenderGraph_Impl : public CRenderGraph
	{
	public:
		//GPUTextureHandle
		virtual TextureHandle NewTextureHandle(GPUTextureDescriptor const& textureDesc) override;
		virtual TextureHandle RegisterWindowBackbuffer(WindowHandle* window) override;
		virtual GPUTextureDescriptor const& GetTextureDescriptor(TextureHandle const& handle) const override;
		virtual GPUTextureDescriptor const& GetTextureDescriptor(TIndex descID) const override { return m_TextureDescriptorIDPool.DescIDToDesc(descID); }
		//GPUBufferHandle
		virtual GPUBufferHandle NewGPUBufferHandle(EBufferUsageFlags usageFlags
			, uint64_t count
			, uint64_t stride) override;
		virtual void ScheduleBufferData(GPUBufferHandle bufferHandle, uint64_t bufferOffset, uint64_t dataSize, void* pData) override;
		virtual GPUBufferDescriptor const& GetGPUBufferDescriptor(TIndex handleID) const override;
		//ConstantSetHandle
		virtual ShaderConstantSetHandle NewShaderConstantSetHandle(ShaderConstantsBuilder const& builder) override;
		//ShaderBindingSetHandle
		virtual ShaderBindingSetHandle NewShaderBindingSetHandle(ShaderBindingBuilder const& builder) override;
		//RenderPass
		virtual CRenderpassBuilder& NewRenderPass(castl::vector<CAttachmentInfo> const& inAttachmentInfo) override;



		virtual uint32_t GetRenderNodeCount() const override;
		virtual CRenderpassBuilder const& GetRenderPass(uint32_t nodeID) const override;

		virtual uint32_t GetTextureHandleCount() const override { return m_TextureHandleIdToInternalInfo.size(); }
		virtual uint32_t GetTextureTypesDescriptorCount() const override { return m_TextureDescriptorIDPool.DescCount(); }

#pragma region Internal Data
		virtual ITextureHandleInternalInfo const& GetGPUTextureInternalData(TIndex handleID) const override
		{
			return m_TextureHandleIdToInternalInfo[handleID];
		}

		virtual ITextureHandleInternalInfo& GetGPUTextureInternalData(TIndex handleID) override
		{
			return m_TextureHandleIdToInternalInfo[handleID];
		}

		virtual IGPUBufferInternalData& GetGPUBufferInternalData(TIndex handleID) override
		{
			return m_GPUBufferInternalInfo[handleID];
		}

		virtual IGPUBufferInternalData const& GetGPUBufferInternalData(TIndex handleID) const override
		{
			return m_GPUBufferInternalInfo[handleID];
		}

		virtual IShaderConstantSetData& GetConstantSetData(TIndex constantSetIndex) override
		{
			return m_ShaderConstantSetDataList[constantSetIndex];
		}

		virtual IShaderBindingSetData* GetBindingSetData(TIndex bindingSetIndex) override
		{
			return &m_ShaderBindingSetDataList[bindingSetIndex];
		}
		virtual IShaderBindingSetData const* GetBindingSetData(TIndex bindingSetIndex) const override
		{
			return &m_ShaderBindingSetDataList[bindingSetIndex];
		}
#pragma endregion

		virtual uint32_t GetBindingSetDataCount() const override
		{
			return m_ShaderBindingSetDataList.size();
		}

		virtual ShaderBindingBuilder const& GetShaderBindingSetDesc(TIndex descID) const override 
		{ 
			return m_BindingDescriptorIDPool.DescIDToDesc(descID); 
		}

		virtual castl::unordered_map<WindowHandle*, TIndex> const& WindowHandleToTextureIndexMap() const override
		{
			return m_RegisteredWindowHandleIDs;
		}

		virtual TIndex WindowHandleToTextureIndex(castl::shared_ptr<WindowHandle> handle) const override;

		virtual uint32_t GetGPUBufferHandleCount() const override 
		{
			return m_GPUBufferDescriptorIDPool.DataIDCount();
		}

		virtual uint32_t GetConstantSetCount() const override 
		{
			return m_ConstantSetDescriptorIDPool.DataIDCount();
		}

		virtual ShaderConstantsBuilder const& GetShaderConstantDesc(TIndex descID) const override
		{
			return m_ConstantSetDescriptorIDPool.DescIDToDesc(descID);
		}
#pragma region Descriptor Getters
		InternalDataManager<GPUTextureDescriptor> const& GetGPUTextureDescirptorPool() const
		{
			return m_TextureDescriptorIDPool;
		}

		InternalDataManager<GPUBufferDescriptor> const& GetGPUBufferDescirptorPool() const
		{
			return m_GPUBufferDescriptorIDPool;
		}

		InternalDataManager<ShaderConstantsBuilder> const& GetShaderConstantSetDescirptorPool() const
		{
			return m_ConstantSetDescriptorIDPool;
		}
		ShaderConstantSetDataLayout const& GetShaderConstantSetDataLayout(TIndex descID) const
		{
			return m_ShaderConstantSetDataLayouts[descID];
		}

		InternalDataManager<ShaderBindingBuilder> const& GetShaderBindingSetDescirptorPool() const
		{
			return m_BindingDescriptorIDPool;
		}
#pragma endregion

	private:
		TextureHandle NewTextureHandle_Internal(GPUTextureDescriptor const& textureDesc, WindowHandle* window);
	private:
		std::deque<CRenderpassBuilder> m_RenderPasses;

		castl::vector<TextureHandleInternalInfo> m_TextureHandleIdToInternalInfo;
		castl::unordered_map<WindowHandle*, TIndex> m_RegisteredWindowHandleIDs;
		InternalDataManager<GPUTextureDescriptor> m_TextureDescriptorIDPool;

		castl::vector<GPUBufferData_Internal> m_GPUBufferInternalInfo;
		InternalDataManager<GPUBufferDescriptor> m_GPUBufferDescriptorIDPool;

		castl::vector<ShaderBindingSetData_Internal> m_ShaderBindingSetDataList;
		InternalDataManager<ShaderBindingBuilder> m_BindingDescriptorIDPool;

		castl::vector<ShaderConstantSetData_Internal> m_ShaderConstantSetDataList;
		castl::vector<ShaderConstantSetDataLayout> m_ShaderConstantSetDataLayouts;
		InternalDataManager<ShaderConstantsBuilder> m_ConstantSetDescriptorIDPool;
	};
}