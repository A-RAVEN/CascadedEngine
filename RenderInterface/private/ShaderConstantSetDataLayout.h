#pragma once
#include <map>
#include <CASTL/CAString.h>
#include <CASTL/CAUnorderedMap.h>
class ShaderConstantsBuilder;
namespace graphics_backend
{
	class ShaderConstantSetDataLayout
	{
	public:
		ShaderConstantSetDataLayout(ShaderConstantsBuilder const& inDesc);
		castl::unordered_map<castl::string, castl::pair<uint32_t, uint32_t>> const& GetNameToDataOffsetSize() const;
	private:
		uint32_t m_ReservedDataSize = 0;
		castl::unordered_map<castl::string, castl::pair<uint32_t, uint32_t>> m_NameToDataOffsetSize;
	};
}