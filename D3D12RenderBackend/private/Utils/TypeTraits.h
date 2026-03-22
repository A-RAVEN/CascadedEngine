#pragma once
#include <CASTL/CATypeTraits.h>
#include <concepts>

namespace graphics_backend
{
	template<typename ValType, typename DescType>
	concept CanInitWithDesc = requires(DescType desc, ValType val)
	{
		val.Init(desc);
	};

	template<typename T, typename...TArgs>
	concept CanInit = requires(T t, TArgs ... args)
	{
		t.Init(args...);
	};

	template<typename T>
	concept CanRelease = requires(T t)
	{
		t.Release();
	};

	template<typename T, typename U>
	concept DerivedFrom = std::derived_from<T, U>;

	template<typename T, typename U>
	concept StrictDerived = std::derived_from<T, U> && !std::is_same_v<T, U>;


}