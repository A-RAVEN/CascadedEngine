#pragma once
// 拒绝夹具：记录带虚函数（csharp-render-binding-foundations 任务 1.3）。
// 期望：生成器非零退出并点名 `RejectVirtualRecord` 与"虚函数"。
#include <stdint.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	struct RejectVirtualRecord
	{
		uint32_t value = 0;

		virtual uint32_t Score() const { return value; }

		virtual ~RejectVirtualRecord() = default;
	};

	CA_BIND()
	class IRejectVirtualRecord
	{
	public:
		virtual RejectVirtualRecord Echo(RejectVirtualRecord const& value) = 0;
		virtual ~IRejectVirtualRecord() = default;
	};
}
