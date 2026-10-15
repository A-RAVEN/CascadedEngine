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
#include <HashKeys.h>

using VKHashVal = cahash::hash256;

using VKShaderCodeHashVal = cahash::hash256;

template<typename T>
struct TypedVKHashVal
{
	VKHashVal hashVal;
	auto operator<=>(TypedVKHashVal const& other) const = default;
};

// `TypedVKHashVal<T>` 是哈希键链上的一环（`PipelineLayoutCache::descriptorSetLayouts` 的元素），
// 它是个值语义的包装：算它包着的哈希值。手写而不进生成表 —— 这个头把整套 Vulkan 头拖进来，不是
// 生成器能解析的目标；设计给的替代做法就是写一份 `custom_hash_trait` 特化。
namespace cacore::codegen
{
	template<typename T>
	struct custom_hash_trait<TypedVKHashVal<T>>
	{
		static void hash(TypedVKHashVal<T> const& obj, auto& hasher)
		{
			hasher.hash(obj.hashVal);
		}
	};

	// Vulkan-Hpp 的 `vk::Flags<BitType>`：`vk::PipelineLayoutCreateFlags` / `vk::DescriptorSetLayoutCreateFlags` /
	// `vk::PipelineInputAssemblyStateCreateFlags` … 全是它的别名。整个对象就是一个掩码字段，没有填充字节、
	// 也没有多种表示，按它自己的字节算哈希是对的。一条**泛化**特化覆盖全部这类类型 —— 它们是第三方类型、
	// 不能改源码，只能在这里显式声明「按字节算」；本 change 之后运行时不再有"平凡可拷贝就悄悄按字节"那条兜底，
	// 所以这种声明是必须的（没有它，三个 Vulkan 缓存类型的哈希链会当场编译失败）。
	template<typename BitType>
	struct custom_hash_trait<vk::Flags<BitType>>
	{
		static void hash(vk::Flags<BitType> const& obj, auto& hasher)
		{
			static_assert(::std::has_unique_object_representations_v<vk::Flags<BitType>>,
			              "vk::Flags 的布局与本特化的假设不符（它应当只有一个掩码字段、没有填充字节）");
			hasher.hash_raw(&obj, sizeof(obj));
		}
	};
}

template<typename T>
TypedVKHashVal<T> VKHashFunc(T const& obj)
{
	return TypedVKHashVal<T>{cacore::hash_256<T>{}(obj)};
}

// 三个 Vulkan-Hpp 的值类型：它们只在上面那几份手写特化的**容器元素**位置上出现
// （`castl::vector<vk::VertexInputBindingDescription>` 等）。都是"若干 `uint32_t` / 枚举"的小结构、
// 没有填充字节，按整块字节算哈希是对的 —— 宏里那条断言就是这条读数。第三方类型不能改源码，
// 只能在这个模块的公共头里显式声明（本 change 之后运行时不再有"平凡可拷贝就悄悄按字节"那条兜底）。
CA_HASH_BY_BYTES(::vk::VertexInputBindingDescription)
CA_HASH_BY_BYTES(::vk::VertexInputAttributeDescription)
CA_HASH_BY_BYTES(::vk::PushConstantRange)

constexpr bool VULKAN_SUPPORT_PIPELINE_LIBRARY = true;
