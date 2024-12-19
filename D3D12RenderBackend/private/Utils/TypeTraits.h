#pragma once

namespace graphics_backend
{
	template<typename ValType, typename DescType>
	concept CanInitWithDesc = requires(DescType desc, ValType val)
	{
		val.Init(desc);
	};
}