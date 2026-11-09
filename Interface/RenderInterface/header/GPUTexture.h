#pragma once
// 绑定标注宏（CA_BIND）：这个头自己也进绑定面（2.2）。
#include <CodeGenMeta.h>
#include <CASTL/CAAlgorithm.h>
#include "Common.h"

namespace graphics_backend
{
	struct GPUTextureDescriptor
	{
		ETextureFormat format;
		ETextureType textureType;
		//ETextureAccessTypeFlags accessType;
		uint32_t width;
		uint32_t height;
		uint32_t layers;
		uint32_t mipLevels;
		EMultiSampleCount samples;

		auto operator<=>(const GPUTextureDescriptor&) const = default;

		static GPUTextureDescriptor Create(
			uint32_t width, uint32_t height
			, ETextureFormat format
			//, ETextureAccessTypeFlags accessType
			, ETextureType textureType = ETextureType::e2D
			, uint32_t layers = 1
			, uint32_t mipLevels = 1
			, EMultiSampleCount samples = EMultiSampleCount::e1)
		{
			GPUTextureDescriptor desc{};
			desc.width = width;
			desc.height = height;
			desc.format = format;
			desc.textureType = textureType;
			//desc.accessType = accessType;
			desc.layers = layers;
			desc.mipLevels = mipLevels;
			desc.samples = samples;
			return desc;
		}
	};

	struct GPUTextureSwizzle
	{
		EColorChannel r;
		EColorChannel g;
		EColorChannel b;
		EColorChannel a;

		auto operator<=>(const GPUTextureSwizzle&) const = default;

		constexpr static GPUTextureSwizzle Create(EColorChannel r = EColorChannel::eR
			, EColorChannel g = EColorChannel::eG
			, EColorChannel b = EColorChannel::eB
			, EColorChannel a = EColorChannel::eA)
		{
			GPUTextureSwizzle desc{};
			desc.r = r;
			desc.g = g;
			desc.b = b;
			desc.a = a;
			return desc;
		}

		constexpr static GPUTextureSwizzle SingleChannel(EColorChannel channel)
		{
			return Create(channel, channel, channel, channel);
		}
	};

	struct GPUTextureView
	{
		ETextureAspect aspect;
		uint32_t baseMip;
		uint32_t mipCount;
		uint32_t baseLayer;
		uint32_t layerCount;
		GPUTextureSwizzle swizzle;

		auto operator<=>(const GPUTextureView&) const = default;

		constexpr static GPUTextureView Create(
			uint32_t baseMip = 0
			, uint32_t mipCount = 0
			, uint32_t baseLayer = 0
			, uint32_t layerCount = 0
			, ETextureAspect aspect = ETextureAspect::eDefault
			, GPUTextureSwizzle swizzle = GPUTextureSwizzle::Create())
		{
			GPUTextureView desc{};
			desc.baseMip = baseMip;
			desc.mipCount = mipCount;
			desc.baseLayer = baseLayer;
			desc.layerCount = layerCount;
			desc.aspect = aspect;
			desc.swizzle = swizzle;
			return desc;
		}

		//Obsolete
		constexpr static GPUTextureView CreateDefaultForRenderTarget(ETextureFormat format, uint32_t mip = 0u, uint32_t layer = 0u)
		{
			GPUTextureView desc{};
			desc.baseMip = mip;
			desc.mipCount = 1;
			desc.baseLayer = layer;
			desc.layerCount = 1;
			desc.aspect = ETextureAspect::eDefault;
			desc.swizzle = GPUTextureSwizzle::Create();
			return desc;
		}

		constexpr static GPUTextureView CreateDefaultForRenderTarget(uint32_t mip = 0u, uint32_t layer = 0u)
		{
			GPUTextureView desc{};
			desc.baseMip = mip;
			desc.mipCount = 1;
			desc.baseLayer = layer;
			desc.layerCount = 1;
			desc.aspect = ETextureAspect::eDefault;
			desc.swizzle = GPUTextureSwizzle::Create();
			return desc;
		}

		constexpr static GPUTextureView CreateDefaultForSampling(ETextureFormat format, GPUTextureSwizzle swizzle = GPUTextureSwizzle::Create())
		{
			bool hasDepth = FormatHasDepth(format);
			GPUTextureView desc{};
			desc.baseMip = 0;
			desc.mipCount = ~0;
			desc.baseLayer = 0;
			desc.layerCount = ~0;
			desc.aspect = hasDepth ? ETextureAspect::eDepth : ETextureAspect::eDefault;
			desc.swizzle = swizzle;
			return desc;
		}

		constexpr void Sanitize(GPUTextureDescriptor const& textureDesc)
		{
			baseMip = castl::clamp(baseMip, 0u, textureDesc.mipLevels - 1);
			uint32_t remainCount = textureDesc.mipLevels - baseMip;
			mipCount = mipCount == 0u ? remainCount : (castl::min)(mipCount, remainCount);
			baseLayer = castl::clamp(baseLayer, 0u, textureDesc.layers - 1);
			remainCount = textureDesc.layers - baseLayer;
			layerCount = layerCount == 0u ? remainCount : (castl::min)(layerCount, remainCount);
		}
	};

	// 纹理接口进绑定面（csharp-native-direct-binding 2.2 / design D1）：描述符按值镜像、名字走两段式。
	CA_BIND()
	class GPUTexture
	{
	public:
		virtual GPUTextureDescriptor const& GetDescriptor() const = 0;
		virtual void SetName(castl::string const& name) = 0;
		virtual castl::string const& GetName() const = 0;
	};
}

// `graphics_backend::GPUTextureView` 与它的 `GPUTextureSwizzle` 都是哈希键（`castl::unordered_map<GPUTextureView, …>`）。
//
// `GPUTextureSwizzle`：四个 `EColorChannel`，没有填充字节 → 按整块字节算是对的（下面那条断言就是读数）。
CA_HASH_BY_BYTES(::graphics_backend::GPUTextureSwizzle)

// `GPUTextureView` **不能**按整块字节算：读数是 `std::has_unique_object_representations_v` 为**假**
// （`ETextureAspect` 与后面的 `uint32_t` 之间有填充字节）—— 实测把它写成 `CA_HASH_BY_BYTES` 时，
// 宏里那条断言当场把构建挡住。而它的 `operator<=>` 是按成员比的，按字节算会让"成员相等、填充不同"的
// 两个 view 落到不同的桶里。所以按声明顺序逐字段手写特化（这是本 change 说的第 ② 条出路）。
namespace cacore::codegen
{
	template<>
	struct custom_hash_trait<::graphics_backend::GPUTextureView>
	{
		static void hash(::graphics_backend::GPUTextureView const& obj, auto& hasher)
		{
			hasher.hash(obj.aspect);
			hasher.hash(obj.baseMip);
			hasher.hash(obj.mipCount);
			hasher.hash(obj.baseLayer);
			hasher.hash(obj.layerCount);
			hasher.hash(obj.swizzle);
		}
	};
}