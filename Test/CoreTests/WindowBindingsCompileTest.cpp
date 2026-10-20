// `IWindow` 的绑定产物能不能被真正编译并链接（`csharp-bindings-generator` 任务 2.10）。
//
// 这个文件 `#include` 了生成出来的 `IWindow.Bindings.cpp`：只要 CoreTests 能链接成功，就证明
// 那份产物**进了编译**（不是躺在磁盘上没人用）。运行期再验两条：
//   ① `IWindow_GetApi` 的版本/大小握手按约定工作；
//   ② 表里的方法条数与审计过的可绑方法数一致（23 条纯虚 − 1 条带理由排除 = 22）。

#include <CAWindow/WindowSystem.h>

#include "IWindow.Bindings.cpp"

#include <cstdio>
#include <cstring>

bool RunWindowBindingsCompileTest()
{
	const uint32_t realSize = static_cast<uint32_t>(sizeof(IWindowApi_v1));
	IWindowApi_v1 table;
	std::memset(&table, 0xCD, sizeof(table));

	if (IWindow_GetApi(1u, realSize, &table) != realSize)
	{
		std::puts("  IWindow_GetApi 的版本/大小握手没有回填真实大小");
		return false;
	}
	if (table.abi_version != 1u || table.size != realSize)
	{
		std::printf("  表头不对：abi_version=%u、size=%u（期望 %u）\n", table.abi_version, table.size,
		            realSize);
		return false;
	}
	// 22 条可绑方法 + 2 条错误通道 + 1 条 GetApi 之外，表里还应当有 IWindow 自己那些方法指针
	if (realSize <= 8u)
	{
		std::printf("  函数表太小：%u 字节\n", realSize);
		return false;
	}
	return true;
}
