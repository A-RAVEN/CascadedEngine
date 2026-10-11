//
// EmitReflectionTest.cpp — the reflection emitter's output actually compiles,
// and its fieldCount agrees with the number written down here by hand (non-vacuous).
//
// `EmitSample.codegen.h`（一个类型一份，反射 / 序列化 / 哈希三节都在里面）是 `CodeGen.exe` 在构建期按
// `--emit-all --out-dir` 现场生成的，住在构建目录里，源码树里没有入库副本。放在这里 include —— 类型定义
// 之后 —— 必须能编译，且 `static_assert_abi_consistency<codegen_demo::EmitSample>()` 必须通过
// （它断言生成的名字非空，即"这一份生成物在本编译单元里可见"）。
// 字段数与下面那个字面量 4 对齐：发射器与类型一旦漂移，这个编译单元就是硬错误。
//
// "生成的代码符合预期"这一半不再靠入库黄金样本对拍 —— 生成物只落构建目录、每次构建现场生成，对拍的是
// `-o` 单节输出与改造前同名产物的字节（本 change 的 228 次对拍），以及下面这些字段数断言。

#include <CodeGenMeta.h>
#include <CodeGenTypeRegistry.h>
#include <cstdio>
#include <cstring>
#include "TestEmit.h"                        // codegen_demo::EmitSample (Tools/CodeGen on include path)
#include "EmitSample.codegen.h"     // generator output (构建目录的 codegen/Test_CoreTests/)

namespace
{
    // Force instantiation of the ABI guard: it must see a generated name (a missing generated header
    // would make this a build error).
    [[maybe_unused]] constexpr bool force_abi_guard_instantiation =
        (cacore::codegen::static_assert_abi_consistency<codegen_demo::EmitSample>(), true);

    // `EmitSample` 有 4 个成员（`a` / `b` / `c` / `d`）；期望值写死在这里，不依赖任何旧描述符。
    static_assert(cacore::codegen::TypeDescriptor<codegen_demo::EmitSample>::fieldCount == 4,
        "emitted fieldCount must be 4 (EmitSample has a, b, c, d)");

    // Emitted metadata is linear data read back by name/offset/kind (matches the CodeGenMetaTest probe).
    static_assert(cacore::codegen::TypeDescriptor<codegen_demo::EmitSample>::name != nullptr,
        "task-5.1: emitted type must carry a runtime-readable name");

    static_assert(cacore::codegen::TypeDescriptor<codegen_demo::EmitSample>::fields() != nullptr,
        "task-5.1: emitted descriptor must expose a non-null field array");
}

// -----------------------------------------------------------------------------
// optional runtime TypeRegistry. Runtime (not static_assert) because lookup-by-name is a
// runtime behavior, mirroring IModuleManager::GetInstance(NameHash): key=name, null on a miss.
// Called from CoreTests' main (SerializerTest.cpp). Returns true on full pass.
// -----------------------------------------------------------------------------
bool RunTypeRegistryTest()
{
    cacore::codegen::TypeRegistry::registerTypeDescriptor<codegen_demo::EmitSample>();

    const cacore::codegen::CodeGenTypeInfo* got =
        cacore::codegen::TypeRegistry::find("codegen_demo::EmitSample");
    bool ok = got != nullptr;
    ok = ok && got->fieldCount == 4;
    ok = ok && got->name != nullptr && std::strcmp(got->name, "codegen_demo::EmitSample") == 0;
    ok = ok && got->fields != nullptr && std::strcmp(got->fields[0].name, "a") == 0;

    if (!ok)
    {
        // Report THIS assertion's own expectation versus what the emitted table actually carries. A bare
        // "FAILED" line is not enough: four other checks in this same function feed the same `ok`, so
        // without the expected/actual values a red state caused by a different check is indistinguishable
        // from one caused by the field-name check (goal item 1: each red state must carry its own value).
        const char* gotType = (got != nullptr && got->name != nullptr) ? got->name : "(null)";
        const char* gotF0 =
            (got != nullptr && got->fields != nullptr && got->fields[0].name != nullptr)
                ? got->fields[0].name
                : "(null)";
        std::printf(
            "TypeRegistry detail: typeName expected \"codegen_demo::EmitSample\", got \"%s\"; "
            "fields[0].name expected \"a\", got \"%s\"; fieldCount=%zu (expected 4)\n",
            gotType, gotF0, got != nullptr ? got->fieldCount : static_cast<std::size_t>(0));
    }

    // Negative oracle: an unregistered name must resolve to null.
    const bool negativeOk = cacore::codegen::TypeRegistry::find("no_such_type") == nullptr;

    // clear() must drop everything.
    cacore::codegen::TypeRegistry::clear();
    const bool cleared = cacore::codegen::TypeRegistry::find("codegen_demo::EmitSample") == nullptr;

    return ok && negativeOk && cleared;
}


