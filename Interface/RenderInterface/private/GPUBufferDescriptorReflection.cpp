// The FIRST real engine type whose generated metadata is compiled and asserted by a real
// target (the `Rendering` static library). Before this file existed, the generator's output was only
// byte-compared against a committed copy: deleting or replacing it caused no build reaction at all.
//
// What this unit proves, and nothing more:
//   * the generated reflection header for `graphics_backend::GPUBufferDescriptor` compiles;
//   * its field table agrees with the real layout (offsets come from `offsetof`, so they are the
//     compiler's own numbers) and with the field count written down here by hand;
//   * the type name and the member names in the table match the source (compile-time string compare —
//     the engine's own ABI guard does NOT check names).
#include <GPUBuffer.h>                        // graphics_backend::GPUBufferDescriptor (the real type)
#include "GPUBufferDescriptor.Reflection.h"   // generator output: `generated/` is on this target's
                                              // include path (no relative path needed)
#include <CodeGenMeta.h>                      // cacore::codegen::TypeDescriptor + the ABI guard

namespace
{
    using T = graphics_backend::GPUBufferDescriptor;

    constexpr bool sameText(const char* a, const char* b)
    {
        if (a == nullptr || b == nullptr) return false;
        while (*a != '\0' && *b != '\0')
        {
            if (*a != *b) return false;
            ++a;
            ++b;
        }
        return *a == *b;
    }

    // Field count: the generated table against the number written down here by hand (`count`, `stride`).
    // 这条断言就是"改了类型没重新生成"会红的地方 —— 期望值是写死的，不依赖任何旧描述符。
    static_assert(cacore::codegen::TypeDescriptor<T>::fieldCount == 2,
                  "generated field count for GPUBufferDescriptor is not 2 — regenerate the header");

    // Offsets: the generated table stores `offsetof`, i.e. the compiler's own numbers; keeping both
    // sides asserts that the table is about THIS type.
    static_assert(cacore::codegen::TypeDescriptor<T>::fields()[0].offset == offsetof(T, count),
                  "generated offset for 'count' does not match the real layout");
    static_assert(cacore::codegen::TypeDescriptor<T>::fields()[1].offset == offsetof(T, stride),
                  "generated offset for 'stride' does not match the real layout");

    // Names: checked at compile time here (the framework's guard only checks that a name exists).
    static_assert(sameText(cacore::codegen::TypeDescriptor<T>::name,
                           "graphics_backend::GPUBufferDescriptor"),
                  "generated type name does not match graphics_backend::GPUBufferDescriptor");
    static_assert(sameText(cacore::codegen::TypeDescriptor<T>::fields()[0].name, "count"),
                  "generated member name [0] is not 'count'");
    static_assert(sameText(cacore::codegen::TypeDescriptor<T>::fields()[1].name, "stride"),
                  "generated member name [1] is not 'stride'");

    // The framework's own guard, instantiated for a real type for the first time.
    static_assert((cacore::codegen::static_assert_abi_consistency<T>(), true),
                  "codegen ABI guard failed for graphics_backend::GPUBufferDescriptor");
}
