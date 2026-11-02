#pragma once
// 拒绝夹具：包装值返回方向（csharp-render-binding-foundations 任务 1.3）。
// 期望：生成器非零退出并点名 `NameHash` 与"返回"。
#include <stdint.h>

#include "BindableShapes.RenderRejectTypes.h"
#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	CA_BIND()
	class IRejectWrapperReturn
	{
	public:
		virtual cacore::NameHash GetName() const = 0;
		virtual ~IRejectWrapperReturn() = default;
	};
}
