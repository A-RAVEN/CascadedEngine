#pragma once
// 拒绝夹具 ①：「模板 / 泛型」类不能作为绑定对象 —— 生成器必须非零退出并点名 ①。

#include <stdint.h>
#include <CodeGenMeta.h>

namespace ca_bind_fixture
{
	template <typename T>
	CA_BIND()
	class IBindRowTplReject
	{
	public:
		virtual void Set(T value) = 0;
		virtual ~IBindRowTplReject() = default;
	};
}
