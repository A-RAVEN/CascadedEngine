#pragma once
// 拒绝夹具（codegen-native-binding-prereqs 任务 3.2）：参数记录以**非 const 引用出参**出现
// （`OutRecord& out`）。本批只定义输入方向；出参方向的容器 MUST 生成期拒绝并留名。
//
// 这个头只被 CoreTests 的拒绝探针起一次 CodeGen.exe，不参与构建的绑定生成。
#include <stdint.h>
#include <vector>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	struct RejectOutRecord
	{
		std::vector<uint32_t> values;
	};

	CA_BIND()
	class IBindRejectBlockOut
	{
	public:
		virtual void Fill(RejectOutRecord& out) = 0;
		virtual ~IBindRejectBlockOut() = default;
	};
}
