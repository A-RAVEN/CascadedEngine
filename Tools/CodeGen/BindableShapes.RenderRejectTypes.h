#pragma once
// 生成期拒绝夹具的共享前向声明（csharp-render-binding-foundations 任务 1.3）。
//
// 这些声明单独放在这份头里，是为了让**被解析的主文件**里不出现 `template` 这个词：
// 生成器的文本扫描一旦在某个 `CA_BIND` 之前看到 `template`，就会把紧随的接口判成
// "① 模板/泛型"并给出与本节要验证的原因无关的诊断。主文件只 include 本头，扫描文本里
// 就没有那个关键字，拒绝原因仍然是被测的那一条。
#include <stdint.h>

namespace castl
{
	template<typename T>
	class weak_ptr;
}

namespace cacore
{
	template<typename T, int CompareMode = 0, typename hashAlg = void>
	struct HashObj;

	struct NameHash;
}
