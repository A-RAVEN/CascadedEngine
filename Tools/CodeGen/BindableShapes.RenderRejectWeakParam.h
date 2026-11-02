#pragma once
// 拒绝夹具：`weak_ptr<T>` 入参（csharp-render-binding-foundations 任务 1.3）。
// 期望：生成器非零退出并点名参数 `value` 与 `weak_ptr`；不得被解释为拒绝弱返回句柄。
#include <stdint.h>

#include "BindableShapes.RenderRejectTypes.h"
#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	CA_BIND()
	class IRejectWeakParam
	{
	public:
		virtual void Use(castl::weak_ptr<int> const& value) = 0;
		virtual ~IRejectWeakParam() = default;
	};
}
