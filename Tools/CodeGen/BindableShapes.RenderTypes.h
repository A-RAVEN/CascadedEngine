#pragma once
// 渲染基础夹具的共享值类型（csharp-render-binding-foundations 任务 1.1）。
//
// 这个头只放**值类型声明**，不放 `CA_BIND` 接口：`BindableShapes.RenderFoundation.h` 与
// `BindableShapes.RenderResource.h` / `BindableShapes.RenderOwner.h` 都 include 它，跨接口
// 共享同一份公开 C# 类型。它同时是 `CODEGEN_ANNOTATED` 的解析入口 —— `RenderHashable` 带
// `CA_REFLECT(serializable)`，消费头 `BindableShapes.RenderFoundation.h` 再 include 生成的
// `RenderHashable.codegen.h`。分成两个头是为了避开"生成物被自己的生成输入包含"的先后死锁：
// 反射生成解析本文件（不包含生成物），绑定生成才解析包含生成物的消费头。
#include <stdint.h>

#include <uenum.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	// 普通枚举：记录字段里按真实底层类型（uint16_t）镜像，方法级按 int32_t 投影。
	enum class ERenderMode : uint16_t
	{
		None = 0,
		Fill = 1,
		Wire = 2,
	};

	// 标志位枚举：记录字段里按 `uenum::EnumFlags<ERenderFlag>::MaskType`（uint16_t）镜像；
	// 公开 C# 枚举带 [Flags]。
	enum class ERenderFlag : uint16_t
	{
		None = 0,
		Depth = 1,
		Blend = 2,
		Cull = 4,
	};

	using ERenderFlags = uenum::EnumFlags<ERenderFlag>;

	// `HashObj<T>` 的 T：除了满足公开数据记录条件，还必须有消费翻译单元可见的显式哈希来源。
	// 这里用 `CA_REFLECT(serializable)`，由 build.py 的 CODEGEN_ANNOTATED 出码；消费头负责
	// include 生成的 `RenderHashable.codegen.h`。
	CA_REFLECT(serializable)
	struct RenderHashable
	{
		uint32_t first;
		uint32_t second;
	};

	// 平坦公开数据记录：bool + 窄枚举 + 记录级标志位 + 定宽整数。
	// 允许静态工厂与非虚便利查询，不改变数据布局。
	struct RenderDesc
	{
		bool visible;
		uint32_t id;
		ERenderMode mode;
		ERenderFlags flags;
		uint16_t weight;

		static RenderDesc Make(uint32_t value, uint16_t layer)
		{
			RenderDesc desc{};
			desc.id = value;
			desc.weight = layer;
			return desc;
		}

		uint32_t Score() const
		{
			return id + static_cast<uint32_t>(weight);
		}
	};

	// 嵌套记录：外层记录的字段偏移必须递归保留，不能摊平成匿名字节。
	struct RenderNested
	{
		RenderDesc desc;
		float scale;
	};
}
