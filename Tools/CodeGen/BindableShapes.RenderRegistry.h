#pragma once
// 登记表夹具（render-interface-merge 任务 1.1）。
//
// 这个头只做一件事：给"进入宿主登记表的接口"演练用。两个接口带**夹具专用号段**的编号
// （8、9，真实接口占 1–7），用来证明：
//   · `--emit-binding` 能从 `CA_BIND(编号)` 读出编号并写进登记表产物；
//   · 进表接口缺编号 / 两个接口同号时生成器非零退出并点名；
//   · 新增一个进表接口的手写改动面只有任务 5.1 列出的那四处。
//
// 既有夹具头（本目录下其它 `BindableShapes*.h`）维持无号 `CA_BIND()` 现状，按 design D2
// 的豁免口径不进任何登记表；这一份是**显式**的登记表输入，所以写编号。
#include <stdint.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	// 编号 8：最小可投影形状（uint32_t 读写 + 常量查询）。
	CA_BIND(8)
	class IBindRegistry8
	{
	public:
		virtual uint32_t GetNumber() const = 0;
		virtual void SetNumber(uint32_t value) = 0;
		virtual uint32_t GetNumberDoubled() const = 0;
		virtual ~IBindRegistry8() = default;
	};

	// 编号 9：第二个接口，用来证明登记表条目按编号排序且互不覆盖。
	CA_BIND(9)
	class IBindRegistry9
	{
	public:
		virtual uint32_t GetNumber() const = 0;
		virtual void SetNumber(uint32_t value) = 0;
		virtual ~IBindRegistry9() = default;
	};
}
