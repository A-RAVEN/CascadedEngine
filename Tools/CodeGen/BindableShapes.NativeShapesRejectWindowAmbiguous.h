#pragma once
// 拒绝夹具（csharp-native-direct-binding 任务 1.1 / design D2）：内存窗配对**不唯一**。
// `void const*` 参数两侧各有一个整型候选（前面 uint64_t lead、后面 uint64_t trail），
// 生成器 MUST 非零退出并点名"多个整型候选"，不许猜。
//
// 这个头只被 CoreTests 的拒绝探针起一次 CodeGen.exe，不参与构建的绑定生成。
#include <stdint.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	CA_BIND()
	class IBindRejectWindowAmbiguous
	{
	public:
		virtual void UploadAmbiguous(uint64_t lead, void const* data, uint64_t trail) = 0;
		virtual ~IBindRejectWindowAmbiguous() = default;
	};
}
