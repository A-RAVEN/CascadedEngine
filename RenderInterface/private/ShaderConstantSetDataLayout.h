#pragma once
#include <map>
class ShaderConstantsBuilder;
namespace graphics_backend
{
	class ShaderConstantSetDataLayout
	{
	public:
		ShaderConstantSetDataLayout(ShaderConstantsBuilder const& inDesc);
		std::map<std::string, std::pair<uint32_t, uint32_t>> const& GetNameToDataOffsetSize() const;
	private:
		uint32_t m_ReservedDataSize = 0;
		std::map<std::string, std::pair<uint32_t, uint32_t>> m_NameToDataOffsetSize;
	};
}