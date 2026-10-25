#pragma once
// 拒绝夹具：这些形状必须让绑定发射器**非零退出**并点名类别，不许生成"看着能编译、调用即崩"的绑定。
// 与 BindableShapes.h 分开的原因：那份头要能整体生成成功（退出码 0），而这里的形状必须被拒绝。
// 矩阵第 11 行（`castl::weak_ptr` → 不投影，归第 ② 类）就在这里。

#include <stdint.h>
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAUniquePtr.h>
#include <CodeGenMeta.h>

namespace ca_bind_fixture
{
	struct BindScalarThing;

	// 第 11 行现在在 `BindableShapes.h` 里是**正向**的弱句柄夹具。这一份改成守"② 类必须写理由"：
	// 返回裸指针、没有所有权说明 ⇒ 必须显式排除，且理由要写清是哪一类。
	CA_BIND()
	class IBindRow11Reject
	{
	public:
		CA_BIND_SKIP(PeekRawThing, "② 所有权不明裸指针：返回裸指针且没有所有权说明（不投影）")
		virtual BindScalarThing* PeekRawThing() = 0;
		virtual ~IBindRow11Reject() = default;
	};
}
