#pragma once

#if defined (_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR 1
#elif defined(__linux__)
#define VK_USE_PLATFORM_XCB_KHR 1
#elif defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif

#ifndef VULKAN_HPP_TYPESAFE_CONVERSION 
#define VULKAN_HPP_TYPESAFE_CONVERSION 1
#endif
//#include <volk.h>

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#endif

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_hash.hpp>
#define VULKAN_API_VERSION_IN_USE VK_API_VERSION_1_3

#include <CACore/CAHash.h>
#include <Hasher.h>

using VKHashVal = cahash::hash256;

using VKShaderCodeHashVal = cahash::hash256;

template<typename T>
struct TypedVKHashVal
{
	VKHashVal hashVal;
	auto operator<=>(TypedVKHashVal const& other) const = default;
};

template<typename T>
TypedVKHashVal<T> VKHashFunc(T const& obj)
{
	return TypedVKHashVal<T>{cacore::hash_256<T>{}(obj)};
}

constexpr bool VULKAN_SUPPORT_PIPELINE_LIBRARY = true;
