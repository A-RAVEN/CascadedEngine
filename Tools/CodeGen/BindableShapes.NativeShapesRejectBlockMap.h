#pragma once
// 拒绝夹具（codegen-native-binding-prereqs 任务 3.2）：参数记录里含映射容器
// （`unordered_map`）。生成器 MUST 非零退出并点名"未实现形状 + 恢复路径"，MUST NOT 静默发桩。
//
// 这个头只被 CoreTests 的拒绝探针起一次 CodeGen.exe，不参与构建的绑定生成。
#include <stdint.h>
#include <unordered_map>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	struct RejectMapRecord
	{
		std::unordered_map<uint32_t, uint32_t> values;
	};

	CA_BIND()
	class IBindRejectBlockMap
	{
	public:
		virtual void Consume(RejectMapRecord const& value) = 0;
		virtual ~IBindRejectBlockMap() = default;
	};
}
