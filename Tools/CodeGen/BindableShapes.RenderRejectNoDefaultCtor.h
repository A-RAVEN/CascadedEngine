#pragma once
// 拒绝夹具：记录没有默认构造函数（csharp-render-binding-foundations 任务 1.3）。
// 期望：生成器非零退出并点名 `RejectNoDefaultRecord` 与"默认构造"。
#include <stdint.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	struct RejectNoDefaultRecord
	{
		explicit RejectNoDefaultRecord(uint32_t initial) : value(initial) {}

		uint32_t value;
	};

	CA_BIND()
	class IRejectNoDefaultRecord
	{
	public:
		virtual RejectNoDefaultRecord Echo(RejectNoDefaultRecord const& value) = 0;
		virtual ~IRejectNoDefaultRecord() = default;
	};
}
