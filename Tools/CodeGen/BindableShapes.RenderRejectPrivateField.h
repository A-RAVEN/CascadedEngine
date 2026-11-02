#pragma once
// 拒绝夹具：记录含非公开字段（csharp-render-binding-foundations 任务 1.3）。
// 期望：生成器非零退出并点名 `RejectPrivateRecord` 与"非公开"。
#include <stdint.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	class RejectPrivateRecord
	{
	public:
		uint32_t Value() const { return m_value; }

	private:
		uint32_t m_value = 0;
	};

	CA_BIND()
	class IRejectPrivateRecord
	{
	public:
		virtual RejectPrivateRecord Echo(RejectPrivateRecord const& value) = 0;
		virtual ~IRejectPrivateRecord() = default;
	};
}
