#pragma once
#include <type_traits>

namespace cacore
{
	template<typename T>
	struct Rect
	{
		T x;
		T y;
		T width;
		T height;
		auto operator<=>(const Rect&) const = default;
	};
}