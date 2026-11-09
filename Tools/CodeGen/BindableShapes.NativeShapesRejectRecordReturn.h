#pragma once
// 拒绝夹具（csharp-native-direct-binding 任务 1.2 / design D8）：返回位置指向
// **不是可序列化记录**的裸指针（这里是夹具资源对象）。"裸指针不可绑"的唯一例外只对
// 可序列化记录开，指向资源对象的裸指针返回 MUST 生成期非零点名，不许静默绑成裸地址。
//
// 这个头只被 CoreTests 的拒绝探针起一次 CodeGen.exe，不参与构建的绑定生成。
#include <stdint.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	// 夹具资源对象：不是可序列化记录（没有容器 / 文本成员），两段式通道不适用。
	struct FixtureResourceObject
	{
		uint64_t handle;
	};

	CA_BIND()
	class IBindRejectRecordReturn
	{
	public:
		virtual FixtureResourceObject const* FindResource(uint32_t id) = 0;
		virtual ~IBindRejectRecordReturn() = default;
	};
}
