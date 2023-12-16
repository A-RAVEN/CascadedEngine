#include "pch.h"
#include "ShaderConstantSetData.h"
#include "CRenderGraphImpl.h"

namespace graphics_backend
{
	void ShaderConstantSetData_Internal::SetValue(std::string const& name, void* pValue)
	{
		auto& layoutInfo = p_RenderGraph->GetShaderConstantSetDataLayout(m_DescriptorID).GetNameToDataOffsetSize();

		auto found = layoutInfo.find(name);
		if (found == layoutInfo.end())
		{
			CA_LOG_ERR("shader variable " + name + " not found in constant buffer");
			return;
		}

		auto localFound = m_NameToOffsetSize.find(name);
		if (localFound == m_NameToOffsetSize.end())
		{
			m_NameToOffsetSize.insert({ name, { m_ScheduledData.size(), found->second.second} });
			m_ScheduledData.resize(m_ScheduledData.size() + found->second.second);
			localFound = m_NameToOffsetSize.find(name);
		}
	
		memcpy(&m_ScheduledData[localFound->second.first]
			, pValue
			, localFound->second.second);
	}

	void const* ShaderConstantSetData_Internal::GetUploadingDataPtr() const
	{
		return m_ScheduledData.data();
	}

	uint32_t ShaderConstantSetData_Internal::GetUploadingDataByteSize() const
	{
		return m_ScheduledData.size();
	}
	void ShaderConstantSetData_Internal::PopulateCachedData(std::vector<std::tuple<std::string, uint32_t, uint32_t>>& outData) const
	{
		outData.resize(m_NameToOffsetSize.size());
		uint32_t id = 0;
		for (auto& data : m_NameToOffsetSize)
		{
			outData[id] = std::make_tuple(data.first, data.second.first, data.second.second);
			++id;
		}
	}
}