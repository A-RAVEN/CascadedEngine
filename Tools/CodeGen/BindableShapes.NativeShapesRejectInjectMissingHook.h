#pragma once
// 拒绝夹具（codegen-native-binding-prereqs 任务 3.3）：**有注入声明但没有钩子声明**。
// 生成器 MUST 非零退出并点名缺 `extern "C" void* CAInject_…();`（装配核对）。
//
// 这个头只被 CoreTests 的拒绝探针起一次 CodeGen.exe，不参与构建的绑定生成。
#include <stdint.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	CA_BIND()
	class IBindRejectInjectMissingHook
	{
	public:
		virtual void Set(int32_t* value) = 0;
		virtual ~IBindRejectInjectMissingHook() = default;
	};
	CA_BIND_INJECT(Set, value)
}
