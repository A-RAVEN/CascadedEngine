#pragma once
// 拒绝夹具（codegen-native-binding-prereqs 任务 3.2）：参数记录里含两层容器
// （`vector<vector<T>>`）。超出"首批见证形状"的覆盖界 ⇒ 生成期非零点名 + 恢复路径。
//
// 这个头只被 CoreTests 的拒绝探针起一次 CodeGen.exe，不参与构建的绑定生成。
#include <stdint.h>
#include <vector>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	struct RejectNestedVectorRecord
	{
		std::vector<std::vector<uint32_t>> values;
	};

	CA_BIND()
	class IBindRejectBlockNested
	{
	public:
		virtual void Consume(RejectNestedVectorRecord const& value) = 0;
		virtual ~IBindRejectBlockNested() = default;
	};
}
