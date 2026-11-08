#pragma once
// 登记表夹具（render-interface-merge 任务 1.1）。
//
// 这个头只做一件事：给"进入宿主登记表的接口"演练用。两个接口占**夹具专用号段**（8、9，
// 真实接口占 1–7；编号写在 Tools/CodeGen/interface-ids.lock 里，标注上不写数字），用来证明：
//   · `--emit-binding` 从锁文件读编号，并写进登记表产物；
//   · 锁里缺名字 / 重号 / 跳号时生成器非零退出并点名；
//   · 新增一个进表接口时，编号由 `CodeGen --update-ids` 发，人不接触数字。
//
// 既有夹具头（本目录下其它 `BindableShapes*.h`）不进任何登记表；这一份是**显式**的登记表输入。
#include <stdint.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	// 夹具号段 8：最小可投影形状（uint32_t 读写 + 常量查询）。
	CA_BIND()
	class IBindRegistry8
	{
	public:
		virtual uint32_t GetNumber() const = 0;
		virtual void SetNumber(uint32_t value) = 0;
		virtual uint32_t GetNumberDoubled() const = 0;
		virtual ~IBindRegistry8() = default;
	};

	// 夹具号段 9：第二个接口，用来证明登记表条目按编号排序且互不覆盖。
	CA_BIND()
	class IBindRegistry9
	{
	public:
		virtual uint32_t GetNumber() const = 0;
		virtual void SetNumber(uint32_t value) = 0;
		virtual ~IBindRegistry9() = default;
	};
}
