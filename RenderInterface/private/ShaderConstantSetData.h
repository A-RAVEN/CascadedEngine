#pragma once
#include <header/CRenderGraph.h>

namespace graphics_backend
{
	class CRenderGraph_Impl;
	class ShaderConstantSetData_Internal : public IShaderConstantSetData
	{
	public:
		ShaderConstantSetData_Internal() = default;
		ShaderConstantSetData_Internal(CRenderGraph_Impl* pRenderGraph, TIndex descIndex) :
			p_RenderGraph(pRenderGraph)
			, m_DescriptorID(descIndex)
		{}
		virtual void SetValue(std::string const& name, void* pValue) override;
		virtual TIndex GetDescID() const override
		{
			return m_DescriptorID;
		}

		virtual void const* GetUploadingDataPtr() const override;
		virtual uint32_t GetUploadingDataByteSize() const override;

		virtual void PopulateCachedData(std::vector<std::tuple<std::string, uint32_t, uint32_t>>& outData) const override;
	private:
		
		std::map<std::string, std::pair<uint32_t, uint32_t>> m_NameToOffsetSize;
		std::vector<uint8_t> m_ScheduledData;
		CRenderGraph_Impl* p_RenderGraph = nullptr;
		TIndex m_DescriptorID = INVALID_INDEX;
	};
}