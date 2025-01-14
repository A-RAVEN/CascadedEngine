#pragma once
#include "CAContainerBase.h"
#if USING_EASTL
#include <EASTL/unordered_set.h>
#else
#include <unordered_set>
#endif

namespace castl
{
	template <typename Key,
		typename Hash = cacore::hash<Key>,
		typename Predicate = equal_to<Key>,
		typename Allocator = std::allocator<Key>>
	using unordered_set = std::unordered_set<Key, Hash, Predicate, Allocator>;

	template <typename Key,
		typename Hash = cacore::hash<Key>,
		typename Predicate = equal_to<Key>,
		typename Allocator = std::allocator<Key>>
	using unordered_multiset = std::unordered_multiset<Key, Hash, Predicate, Allocator>;
}