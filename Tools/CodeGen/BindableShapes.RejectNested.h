#pragma once
// 非法嵌套记录夹具：外层 `BindNestedBadOuter` 是纯标量记录，内层 `BindNestedBadInner` 含
// `castl::string`。生成器 MUST 非零退出并点名完整成员路径（`BindNestedBadOuter.inner.name`），
// 不许只跳过布局清单、仍然以退出码 0 出产物（`csharp-bindable-surface` 规范：
// 嵌套层级里藏有非 blittable 成员时必须拒绝）。
//
// 这份头单独放一个文件：它必须被拒绝，而 `BindableShapes.h` 必须整体生成成功，两者不能混在一起。
#include <stdint.h>
#include <CASTL/CAString.h>
#include <CASTL/CAVector.h>
#include <CodeGenMeta.h>

namespace ca_bind_fixture
{
	struct BindNestedBadInner
	{
		castl::string name;
		uint32_t value;
	};

	struct BindNestedBadOuter
	{
		BindNestedBadInner inner;
		float scale;
	};

	CA_BIND()
	class IBindRowNestedReject
	{
	public:
		virtual BindNestedBadOuter GetBad() const = 0;
		virtual ~IBindRowNestedReject() = default;
	};
}
