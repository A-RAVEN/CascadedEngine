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
		return GPUBufferHandle(handleID);
	}

	void CRenderGraph_Impl::ScheduleBufferData(GPUBufferHandle bufferHandle, uint64_t bufferOffset, uint64_t dataSize, void* pData)
	{
		TIndex handleID = bufferHandle.GetHandleIndex();
		auto bufferData = m_GPUBufferInternalInfo[handleID];
		bufferData.ScheduleBufferData(bufferOffset, dataSize, pData);
	}

	IGPUBufferInternalData const& CRenderGraph_Impl::GetGPUBufferInternalData(GPUBufferHandle const& bufferHandle) const
	{
		TIndex handleID = bufferHandle.GetHandleIndex();
		return m_GPUBufferInternalInfo[handleID];
	}

	GPUBufferDescriptor const& CRenderGraph_Impl::GetGPUBufferDescriptor(GPUBufferHandle const& bufferHandle) const
	{
		return m_GPUBufferDescriptorIDPool.DataIDToDesc(bufferHandle.GetHandleIndex());
	}

	TextureHandle CRenderGraph_Impl::RegisterWindowBackbuffer(std::shared_ptr<WindowHandle> window)
	{
		return NewTextureHandle_Internal(window->GetBackbufferDescriptor(), window);
	}

	CRenderpassBuilder& CRenderGraph_Impl::NewRenderPass(std::vector<CAttachmentInfo> const& inAttachmentInfo)
	{
		return m_RenderPasses.emplace_back(inAttachmentInfo);
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

	TextureHandle CRenderGraph_Impl::TextureHandleByIndex(TIndex index) const
	{
		return TextureHandle{ index };
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

	TextureHandle CRenderGraph_Impl::NewTextureHandle_Internal(GPUTextureDescriptor const& textureDesc, std::shared_ptr<WindowHandle> window)
	{
		if (window != nullptr)
		{
			auto found = m_RegisteredWindowHandleIDs.find(window.get());
			if (found != m_RegisteredWindowHandleIDs.end())
			{
				return TextureHandle(found->second);
			}
		}
		TIndex descID;
		TIndex dataID = m_TextureDescriptorIDPool.RegisterNewData(textureDesc, descID);
		CA_ASSERT(m_TextureHandleIdToInternalInfo.size() == dataID, "invalid ID");
		m_TextureHandleIdToInternalInfo.push_back(TextureHandleInternalInfo{ dataID, window });
		if (window != nullptr)
		{
			m_RegisteredWindowHandleIDs.insert(std::make_pair(window.get(), dataID));
		}
		return TextureHandle(dataID);
	}
}