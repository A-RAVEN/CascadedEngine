#include "pch.h"
#include "CRenderGraphImpl.h"


namespace graphics_backend
{

	TextureHandle CRenderGraph_Impl::NewTextureHandle(GPUTextureDescriptor const& textureDesc)
	{
		return NewTextureHandle_Internal(textureDesc, nullptr);
	}

	GPUBufferHandle CRenderGraph_Impl::NewGPUBufferHandle(EBufferUsageFlags usageFlags, uint64_t count, uint64_t stride)
	{
		TIndex descID;
		TIndex handleID = m_GPUBufferDescriptorIDPool.RegisterNewData(GPUBufferDescriptor{ usageFlags, count, stride }, descID);
		CA_ASSERT(handleID == m_GPUBufferInternalInfo.size(), "invalid ID");
		m_GPUBufferInternalInfo.emplace_back();
		return GPUBufferHandle(this, handleID);
	}

	void CRenderGraph_Impl::ScheduleBufferData(GPUBufferHandle bufferHandle, uint64_t bufferOffset, uint64_t dataSize, void* pData)
	{
		TIndex handleID = bufferHandle.GetHandleIndex();
		auto& bufferData = m_GPUBufferInternalInfo[handleID];
		bufferData.ScheduleBufferData(bufferOffset, dataSize, pData);
	}

	GPUBufferDescriptor const& CRenderGraph_Impl::GetGPUBufferDescriptor(TIndex handleID) const
	{
		return m_GPUBufferDescriptorIDPool.DataIDToDesc(handleID);
	}

	TextureHandle CRenderGraph_Impl::RegisterWindowBackbuffer(WindowHandle* window)
	{
		return NewTextureHandle_Internal(window->GetBackbufferDescriptor(), window);
	}

	CRenderpassBuilder& CRenderGraph_Impl::NewRenderPass(std::vector<CAttachmentInfo> const& inAttachmentInfo)
	{
		return m_RenderPasses.emplace_back(inAttachmentInfo);
	}

	ShaderConstantSetHandle CRenderGraph_Impl::NewShaderConstantSetHandle(ShaderConstantsBuilder const& descriptor)
	{
		uint32_t descID;
		TIndex handleID = m_ConstantSetDescriptorIDPool.RegisterNewData(descriptor, descID);
		if (m_ConstantSetDescriptorIDPool.DescCount() > m_ShaderConstantSetDataLayouts.size())
		{
			m_ShaderConstantSetDataLayouts.emplace_back(descriptor);
		}
		CA_ASSERT(handleID == m_ShaderConstantSetDataList.size(), "invalid ID");
		m_ShaderConstantSetDataList.emplace_back(this, descID);
		return ShaderConstantSetHandle{ this, handleID };
	}

	ShaderBindingSetHandle CRenderGraph_Impl::NewShaderBindingSetHandle(ShaderBindingBuilder const& builder)
	{
		uint32_t descID;
		TIndex dataIndex = m_BindingDescriptorIDPool.RegisterNewData(builder, descID);
		CA_ASSERT(dataIndex == m_ShaderBindingSetDataList.size(), "invalid ID");
		m_ShaderBindingSetDataList.emplace_back(descID);
		return ShaderBindingSetHandle(this, descID, dataIndex);
	}

	GPUTextureDescriptor const& CRenderGraph_Impl::GetTextureDescriptor(TextureHandle const& handle) const
	{
		return m_TextureDescriptorIDPool.DataIDToDesc(handle.GetHandleIndex());
	}

	uint32_t CRenderGraph_Impl::GetRenderNodeCount() const
	{
		return m_RenderPasses.size();
	}

	CRenderpassBuilder const& CRenderGraph_Impl::GetRenderPass(uint32_t nodeID) const
	{
		return m_RenderPasses[nodeID];
	}

	TIndex CRenderGraph_Impl::WindowHandleToTextureIndex(std::shared_ptr<WindowHandle> handle) const
	{
		auto found = m_RegisteredWindowHandleIDs.find(handle.get());
		if (found != m_RegisteredWindowHandleIDs.end())
		{
			return found->second;
		}
		return INVALID_INDEX;
	}

	TextureHandle CRenderGraph_Impl::NewTextureHandle_Internal(GPUTextureDescriptor const& textureDesc, WindowHandle* window)
	{
		if (window != nullptr)
		{
			auto found = m_RegisteredWindowHandleIDs.find(window);
			if (found != m_RegisteredWindowHandleIDs.end())
			{
				return TextureHandle(this, found->second);
			}
		}
		TIndex descID;
		TIndex dataID = m_TextureDescriptorIDPool.RegisterNewData(textureDesc, descID);
		CA_ASSERT(m_TextureHandleIdToInternalInfo.size() == dataID, "invalid ID");
		m_TextureHandleIdToInternalInfo.push_back(TextureHandleInternalInfo{ this,  dataID, window});
		if (window != nullptr)
		{
			m_RegisteredWindowHandleIDs.insert(std::make_pair(window, dataID));
		}
		return TextureHandle(this, dataID);
	}
}