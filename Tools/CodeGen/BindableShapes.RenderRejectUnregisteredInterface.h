#pragma once
// 拒绝夹具：返回未登记的抽象接口（csharp-render-binding-foundations 任务 1.3）。
// 期望：生成器非零退出并点名 `IRejectUnregisteredTarget` 与"已知接口清单"，
// 不能留下引用不存在 C# 类型的产物。
#include <stdint.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	// 有纯虚方法、但没有 CA_BIND，也不在任何生成输入里登记。
	class IRejectUnregisteredTarget
	{
	public:
		virtual uint32_t GetTargetId() = 0;
		virtual ~IRejectUnregisteredTarget() = default;
	};

	CA_BIND()
	class IRejectUnregisteredReturn
	{
	public:
		virtual IRejectUnregisteredTarget* GetTarget() = 0;
		virtual ~IRejectUnregisteredReturn() = default;
	};
}
