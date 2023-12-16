#pragma once
#include <header/CRenderGraph.h>

namespace graphics_backend
{
	class CRenderGraph_Impl;
	class WindowHandle;
	class TextureHandleInternalInfo : public ITextureHandleInternalInfo
	{
	public:
		TextureHandleInternalInfo(CRenderGraph_Impl* p_RenderGraph,  TIndex m_DescID) : 
			p_RenderGraph(p_RenderGraph)
			, m_DescID(m_DescID)
		{}
		TextureHandleInternalInfo(CRenderGraph_Impl* p_RenderGraph, TIndex m_DescID, WindowHandle* pWindowsHandle) : 
			p_RenderGraph(p_RenderGraph)
			, m_DescID(m_DescID)
			, p_WindowsHandle(pWindowsHandle)
		{}
		virtual TIndex GetDescID() const override { return m_DescID; }
		virtual bool IsExternalTexture() const override { return p_WindowsHandle != nullptr; }
		virtual WindowHandle* GetWindowsHandle() const override { return p_WindowsHandle; }
		virtual void ScheduleTextureData(uint64_t textureDataOffset, uint64_t dataSize, void* pData) override;
	private:
		CRenderGraph_Impl* p_RenderGraph = nullptr;
		std::vector<uint8_t> m_ScheduledData;
		TIndex m_DescID = INVALID_INDEX;
		WindowHandle* p_WindowsHandle = nullptr;
	};
}