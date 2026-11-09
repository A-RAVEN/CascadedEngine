#pragma once
// 拒绝夹具（csharp-native-binding 本批 1.5 / design D11）：**键非 NameHash 的映射**成员。
// D11 的 MUST NOT：键非 NameHash 的映射、指向未绑定类型的智能指针、返回位置的映射容器
// 任一出现必须生成期点名拒绝，不许静默丢字段。
//
// 这个头只被 CoreTests 的拒绝探针起一次 CodeGen.exe，不参与构建的绑定生成。
// 用 `std::unordered_map` 而不是 `castl::unordered_map`：拒绝探针的 include 集不含 CASTL 的
// 间接依赖（sha256.h），夹具保持自包含才能只测"键类别"这一件事。
#include <stdint.h>
#include <unordered_map>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	struct RejectIntKeyMapRecord
	{
		// 键是 int32_t：D11 只定义 NameHash 键的命名映射。
		std::unordered_map<int32_t, uint32_t> values;
	};

	CA_BIND()
	class IBindRejectMapKey
	{
	public:
		virtual void Consume(RejectIntKeyMapRecord const& record) = 0;
		virtual ~IBindRejectMapKey() = default;
	};
}
