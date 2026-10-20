#pragma once
// 拒绝夹具 ③：「依赖隐式转换的重载」—— `int32_t` 与 `bool` 两个重载投影成同一个 C ABI 签名，
// 生成器必须非零退出并点名 ③。

#include <stdint.h>
#include <CodeGenMeta.h>

namespace ca_bind_fixture
{
	CA_BIND()
	class IBindRowOverloadReject
	{
	public:
		virtual void Set(int32_t value) = 0;
		virtual void Set(bool value) = 0;
		virtual ~IBindRowOverloadReject() = default;
	};
}
