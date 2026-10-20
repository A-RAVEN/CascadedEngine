#pragma once
// 拒绝夹具（无理由版）：同一个第 11 行形状，`CA_BIND_SKIP` 写了但**没写理由** —— 生成器必须非零退出
// 并点名"排除必须写理由"。与 BindableShapes.Reject.h（带理由、应通过）配对使用。

#include <stdint.h>
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAUniquePtr.h>
#include <CodeGenMeta.h>

namespace ca_bind_fixture
{
	struct BindScalarThing;

	CA_BIND()
	class IBindRow11NoReason
	{
	public:
		CA_BIND_SKIP(PeekThing, "")
		virtual castl::weak_ptr<BindScalarThing> PeekThing() = 0;
		virtual ~IBindRow11NoReason() = default;
	};
}
