#pragma once
// 绑定标注宏（CA_BIND）：这个头从 csharp-native-direct-binding 2.4 起进绑定面（design D12）。
#include <CodeGenMeta.h>
#include <CASTL/CAString.h>
#include "GPUTexture.h"

namespace graphics_backend
{
	struct uint2
	{
		uint32_t x;
		uint32_t y;
	};

	struct int2
	{
		int x;
		int y;
	};

	// 窗口句柄进表（本批 2.4 / design D12）：`GetSizeSafe` 返回 `uint2` 纯标量记录镜像、
	// `GetBackbufferDescriptor` 返回 `GPUTextureDescriptor const&` 按序列化只读副本（D8 同族，
	// const& 即非拥有别名）、`IsValid` 是非纯虚成员（批 A 既有形态）。原生签名零改动。
	CA_BIND()
	class WindowHandle
	{
	public:
		virtual uint2 GetSizeSafe() const = 0;
		virtual GPUTextureDescriptor const& GetBackbufferDescriptor() const = 0;
		virtual bool IsValid() const { return true; }
	};
}
