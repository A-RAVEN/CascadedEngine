#pragma once
// 拒绝夹具（csharp-native-direct-binding 本批 1.6 / design D13）：**返回位置的记录容器**。
// D13 只开"可 blittable 标量/枚举元素"的标量容器两段式；记录容器、含句柄/文本的容器维持不可绑，
// 生成期必须点名，不许静默降级成"按字节拷记录"（那样托管侧会拿到无法解释的字节）。
//
// 这个头只被 CoreTests 的拒绝探针起一次 CodeGen.exe，不参与构建的绑定生成。
#include <stdint.h>

#include <CASTL/CAVector.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	struct RejectRecordElement
	{
		uint32_t id = 0u;
		float weight = 0.0f;
	};

	CA_BIND()
	class IBindRejectRecordContainer
	{
	public:
		// 元素是记录（不是标量/枚举）⇒ 生成期拒绝并点名。
		virtual castl::vector<RejectRecordElement> const& GetRecords() const = 0;
		virtual ~IBindRejectRecordContainer() = default;
	};
}
