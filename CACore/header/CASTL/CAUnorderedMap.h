#pragma once
#include "CAContainerBase.h"
#include <Hasher.h>
#include <EASTL/unordered_map.h>

namespace castl
{
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
}