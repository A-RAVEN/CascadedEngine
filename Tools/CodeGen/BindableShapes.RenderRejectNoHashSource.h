#pragma once
// 拒绝夹具：`HashObj<T>` 的 T 缺少显式哈希来源（csharp-render-binding-foundations 任务 1.3）。
// 期望：生成器在写产物前非零退出并点名 `RejectNoHashRecord` 与"哈希来源"，
// 不能留到 C++ 的静态断言才失败。
#include <stdint.h>

#include "BindableShapes.RenderRejectTypes.h"
#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	struct RejectNoHashRecord
	{
		uint32_t first = 0;
		uint32_t second = 0;
	};

	CA_BIND()
	class IRejectNoHashSource
	{
	public:
		virtual void Consume(cacore::HashObj<RejectNoHashRecord> const& value) = 0;
		virtual ~IRejectNoHashSource() = default;
	};
}
