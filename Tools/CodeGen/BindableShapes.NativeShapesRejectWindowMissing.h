#pragma once
// 拒绝夹具（csharp-native-direct-binding 任务 1.1 / design D2）：内存窗**长度参数缺失**。
// `void const*` 参数两侧都没有整型参数（前为无、后为 bool copy），生成器 MUST 非零退出并
// 点名"没有相邻整型长度参数"，不许把裸指针暴露给托管。
//
// 这个头只被 CoreTests 的拒绝探针起一次 CodeGen.exe，不参与构建的绑定生成。
#include <stdint.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	CA_BIND()
	class IBindRejectWindowMissing
	{
	public:
		virtual void UploadMissing(void const* data, bool copy) = 0;
		virtual ~IBindRejectWindowMissing() = default;
	};
}
