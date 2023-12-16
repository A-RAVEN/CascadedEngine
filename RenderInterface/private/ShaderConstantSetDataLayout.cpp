#include "pch.h"
#include "ShaderConstantSetDataLayout.h"
#include <header/ShaderBindingBuilder.h>

namespace graphics_backend
{
	ShaderConstantSetDataLayout::ShaderConstantSetDataLayout(ShaderConstantsBuilder const& inDesc)
	{
		auto& descriptors = inDesc.GetNumericDescriptors();
		m_NameToDataOffsetSize.clear();
		m_ReservedDataSize = 0;
		for (auto& descPairs : descriptors)
		{
			auto& desc = descPairs.second;
			auto& descName = descPairs.first;
			uint32_t count = desc.count;
			CA_ASSERT(count > 0
				, "descriptor must have at least one element");
			CA_ASSERT(m_NameToDataOffsetSize.find(descName) == m_NameToDataOffsetSize.end()
				, "invalid descriptor name");
			std::pair<size_t, size_t>align_size;
			uint32_t dataSize = desc.x * desc.y * desc.count * sizeof(uint32_t);

			CA_ASSERT(dataSize > 0, "Invalid Data Size");
			m_NameToDataOffsetSize.emplace(descName, std::make_pair(m_ReservedDataSize, dataSize));
			m_ReservedDataSize += dataSize;
		}
	}
	std::map<std::string, std::pair<uint32_t, uint32_t>> const& ShaderConstantSetDataLayout::GetNameToDataOffsetSize() const
	{
		return m_NameToDataOffsetSize;
	}
}