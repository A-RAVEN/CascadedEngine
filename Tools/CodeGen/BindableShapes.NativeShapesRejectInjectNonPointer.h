#pragma once
// 拒绝夹具（codegen-native-binding-prereqs 任务 3.3）：注入声明写在了**非指针参数**上。
// 生成器 MUST 非零退出并点名"不是裸指针"。
//
// 这个头只被 CoreTests 的拒绝探针起一次 CodeGen.exe，不参与构建的绑定生成。
#include <stdint.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	CA_BIND()
	class IBindRejectInjectNonPointer
	{
	public:
		virtual void Set(uint32_t value) = 0;
		virtual ~IBindRejectInjectNonPointer() = default;
	};
	CA_BIND_INJECT(Set, value)
}
