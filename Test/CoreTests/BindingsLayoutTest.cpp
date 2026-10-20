// 布局清单（`csharp-bindings-generator` 任务 2.5）：镜像结构体与 C++ 侧的真实布局必须一致。
//
// 生成物里已经有 `static_assert(sizeof(...) == 8)` 与逐字段的 `static_assert(offsetof(...) == N)` ——
// 那两条是**编译期**判据：把夹具的字段类型改掉却不重新生成，这个文件就编不过。
// 这里再补一层运行期对拍，用两侧的真实 `sizeof` / `offsetof` 互相验一遍。

#include "BindableShapes.h"

#include "IBindRow02.Bindings.cpp"

#include <cstddef>
#include <cstdio>

bool RunBindingsLayoutTest()
{
	// 大小
	if (sizeof(BindScalarThingC) != sizeof(ca_bind_fixture::BindScalarThing))
	{
		std::printf("  BindScalarThingC 大小 %zu，C++ 侧 %zu\n", sizeof(BindScalarThingC),
		            sizeof(ca_bind_fixture::BindScalarThing));
		return false;
	}
	// 逐字段偏移
	if (offsetof(BindScalarThingC, id) != offsetof(ca_bind_fixture::BindScalarThing, id))
	{
		std::printf("  字段 id 偏移不一致：%zu vs %zu\n", offsetof(BindScalarThingC, id),
		            offsetof(ca_bind_fixture::BindScalarThing, id));
		return false;
	}
	if (offsetof(BindScalarThingC, weight) != offsetof(ca_bind_fixture::BindScalarThing, weight))
	{
		std::printf("  字段 weight 偏移不一致：%zu vs %zu\n", offsetof(BindScalarThingC, weight),
		            offsetof(ca_bind_fixture::BindScalarThing, weight));
		return false;
	}
	return true;
}
