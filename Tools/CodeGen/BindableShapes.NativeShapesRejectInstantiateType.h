#pragma once
// 拒绝夹具（csharp-native-direct-binding 任务 1.3 / design D9）：实例化声明里的**类型名拼错**。
// 生成器 MUST 非零退出并点名"类型 … 在翻译单元里找不到"，不许把它带进产物（否则编译器在
// 生成的 thunk 里报一条没有上下文的名字）。
//
// 这个头只被 CoreTests 的拒绝探针起一次 CodeGen.exe，不参与构建的绑定生成。
#include <stdint.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	CA_BIND()
	class IBindRejectInstantiateType
	{
	public:
		template<typename T>
		uint32_t Value(T const& value)
		{
			(void)value;
			return 0u;
		}
		virtual ~IBindRejectInstantiateType() = default;
	};
	// NoSuchType 是故意拼错的类型名：翻译单元里没有这个类型。
	CA_BIND_INSTANTIATE(Value, NoSuchType)
}
