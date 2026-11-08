#pragma once
// 拒绝夹具（codegen-native-binding-prereqs 任务 3.3）：注入声明里的**参数名不存在**。
// 生成器 MUST 非零退出并点名参数名与"不存在"。
//
// 这个头只被 CoreTests 的拒绝探针起一次 CodeGen.exe，不参与构建的绑定生成。
#include <stdint.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	CA_BIND()
	class IBindRejectInjectMissingParam
	{
	public:
		virtual void Set(int32_t* value) = 0;
		virtual ~IBindRejectInjectMissingParam() = default;
	};
	CA_BIND_INJECT(Set, missingName)
}
