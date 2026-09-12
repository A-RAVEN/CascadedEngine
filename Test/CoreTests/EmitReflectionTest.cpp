//
// EmitReflectionTest.cpp — task 5.1 verification: the reflection EMITTER's output actually compiles,
// and its fieldCount agrees with the source-of-truth CA_REFLECTION via the ABI guard (non-vacuous).
//
// The emitted `generated/EmitSample.Reflection.h` is produced by running the CodeGen tool with
// --emit-reflection on Tools/CodeGen/TestEmit.h (commit it as the golden). Including it here — after
// the type is fully defined — must compile, and static_assert_abi_consistency<codegen_demo::EmitSample>()
// must PASS, proving the generated fieldCount == CA_REFLECTION member_count (4). If the emitter and
// the reflection drift apart, this TU is a hard build error (design D4 ABI guard).
//
// This is the "接入后真实类型 member_count 与现有 CATypeDescriptor<T> 吻合" half of the task-5.1
// oracle; the "generates expected code" (golden snapshot) half is verified by diffing the fresh tool
// output against the committed golden.

#include <CodeGenMeta.h>
#include <CodeGenTypeRegistry.h>
#include <cstring>
#include "TestEmit.h"                        // codegen_demo::EmitSample (Tools/CodeGen on include path)
#include "generated/EmitSample.Reflection.h"  // emitter output (Tools/CodeGen/generated)

// Source-of-truth reflection for the SAME type: the ABI guard compares the generated descriptor to
// this, so it must be a real (non-vacuous) pair — has_type_desc<EmitSample> is true because
// CATypeDescriptor<EmitSample>::type is EmitSample (not nullptr_t) and member_count is 4.
CA_REFLECTION(codegen_demo::EmitSample, a, b, c, d);

namespace
{
    // Force instantiation of the ABI guard. If the generated fieldCount != member_count, this is a
    // build error — the whole point. Note has_type_desc<codegen_demo::EmitSample> is true, so the
    // guard compares for real and is not a vacuous pass.
    [[maybe_unused]] constexpr bool force_abi_guard_instantiation =
        (cacore::codegen::static_assert_abi_consistency<codegen_demo::EmitSample>(), true);

    static_assert(cacore::codegen::TypeDescriptor<codegen_demo::EmitSample>::fieldCount ==
                      ::CATypeDescriptor<codegen_demo::EmitSample>::member_count,
        "task-5.1: emitted fieldCount must equal CA_REFLECTION member_count (4)");

    // Emitted metadata is linear data read back by name/offset/kind (matches the CodeGenMetaTest probe).
    static_assert(cacore::codegen::TypeDescriptor<codegen_demo::EmitSample>::name != nullptr,
        "task-5.1: emitted type must carry a runtime-readable name");

    static_assert(cacore::codegen::TypeDescriptor<codegen_demo::EmitSample>::fields() != nullptr,
        "task-5.1: emitted descriptor must expose a non-null field array");
}

// -----------------------------------------------------------------------------
// Task 5.2 — optional runtime TypeRegistry. Runtime (not static_assert) because lookup-by-name is a
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

    // Negative oracle: an unregistered name must resolve to null.
    const bool negativeOk = cacore::codegen::TypeRegistry::find("no_such_type") == nullptr;

    // clear() must drop everything.
    cacore::codegen::TypeRegistry::clear();
    const bool cleared = cacore::codegen::TypeRegistry::find("codegen_demo::EmitSample") == nullptr;

    return ok && negativeOk && cleared;
}

