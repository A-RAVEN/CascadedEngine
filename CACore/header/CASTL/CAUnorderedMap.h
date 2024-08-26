#pragma once
#include "CAContainerBase.h"
#include <Hasher.h>
#if USING_EASTL
#include <EASTL/unordered_map.h>
#else
#include <unordered_map>
#endif

namespace castl
{
#if USING_EASTL
	template <typename Key,
		typename T,
		typename Hash = cacore::hash<Key>,
		typename Predicate = eastl::equal_to<Key>,
		typename Allocator = EASTLAllocatorType,
		bool bCacheHashCode = false>
	using unordered_map = eastl::unordered_map<Key, T, Hash, Predicate, Allocator, bCacheHashCode>;

	template <typename Key,
		typename T,
		typename Hash = cacore::hash<Key>,
		typename Predicate = eastl::equal_to<Key>,
		typename Allocator = EASTLAllocatorType,
		bool bCacheHashCode = false>
	using unordered_multimap = eastl::unordered_multimap<Key, T, Hash, Predicate, Allocator, bCacheHashCode>;
#else
	template <typename Key,
		typename T,
		typename Hash = cacore::hash<Key>,
		typename Predicate = equal_to<Key>,
		typename Allocator = std::allocator<std::pair<const Key, T>>>
	using unordered_map = std::unordered_map<Key, T, Hash, Predicate, Allocator>;

	template <typename Key,
		typename T,
		typename Hash = cacore::hash<Key>,
		typename Predicate = equal_to<Key>,
		typename Allocator = std::allocator<std::pair<const Key, T>>>
	using unordered_multimap = std::unordered_multimap<Key, T, Hash, Predicate, Allocator>;
#endif
}