#pragma once
// 拒绝夹具：包装值嵌入记录（csharp-render-binding-foundations 任务 1.3）。
// 期望：生成器非零退出并点名完整成员路径 `RejectWrapperNestedRecord.name` 与"嵌入记录"。
#include <stdint.h>

#include "BindableShapes.RenderRejectTypes.h"
#include "CodeGenMeta.h"

// `cacore::NameHash` 已在共享前向声明里声明过；这里给出一个最小定义，让记录能按值持有它。
namespace cacore
{
	struct NameHash
	{
		char const* text = nullptr;
	};
}

namespace ca_bind_fixture
{
	struct RejectWrapperNestedRecord
	{
		uint32_t id = 0;
		cacore::NameHash name{};
	};

	CA_BIND()
	class IRejectWrapperNested
	{
	public:
		virtual RejectWrapperNestedRecord Echo(RejectWrapperNestedRecord const& value) = 0;
		virtual ~IRejectWrapperNested() = default;
	};
}
