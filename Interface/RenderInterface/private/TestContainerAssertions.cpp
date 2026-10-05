// The container fixture's generated metadata is CONSUMED here —
// inside a real engine target (`Rendering`), not merely byte-compared against a committed copy. The
// main spec requires exactly this: "generated code SHALL be #included by at least one compile unit of a
// real engine target, with a compile-time assertion on the generated table".
#include "TestContainer.h"        // codegen_containers::Containers (private/, same target)
#include "Containers.Reflection.h" // generator output (generated/, on this target's include path)
#include "Inner.Reflection.h"      // 容器里装结构体
#include "Outer.Reflection.h"
#include "Inner.Serialization.h"   // 元素自己的生成代码：递归读写的落脚点
#include "Outer.Serialization.h"
#include "FixedArrays.Reflection.h"
#include "FixedArrays.Serialization.h"
#include <CodeGenMeta.h>          // cacore::codegen::TypeDescriptor + FieldFlags
#include <CodeGenSerialization.h> // 新运行时自有的容器判据（不再引用旧运行时那份）

namespace
{
    using T = codegen_containers::Containers;
    using cacore::codegen::CodeGenFieldInfo;
    using cacore::codegen::FieldFlags;
    using cacore::codegen::has_flag;
    using cacore::codegen::TypeDescriptor;
    using cacore::codegen::TypeKind;

    constexpr const CodeGenFieldInfo* field(const char* name)
    {
        for (std::size_t i = 0; i < TypeDescriptor<T>::fieldCount; ++i)
            if (TypeDescriptor<T>::fields()[i].name == name) // same literal in the generated table
                return &TypeDescriptor<T>::fields()[i];
        return nullptr;
    }

    // Ordered containers: Container flag only.
    static_assert(has_flag(field("vec")->flags, FieldFlags::Container), "'vec' must be a container");
    static_assert(!has_flag(field("vec")->flags, FieldFlags::Unordered), "'vec' is ordered");
    static_assert(field("vec")->element != nullptr, "'vec' must carry an element description");
    static_assert(field("vec")->element->kind == TypeKind::UInt,
                  "'vec' element must be UInt (canonicalized from `unsigned`)");

    // Unordered map: Container + Unordered, with BOTH key and element descriptions.
    static_assert(has_flag(field("umap")->flags, FieldFlags::Container), "'umap' must be a container");
    static_assert(has_flag(field("umap")->flags, FieldFlags::Unordered), "'umap' must be marked unordered");
    static_assert(field("umap")->key != nullptr && field("umap")->element != nullptr,
                  "'umap' must carry key and element descriptions");
    static_assert(field("umap")->key->kind == TypeKind::UInt, "'umap' key must be UInt");
    static_assert(field("umap")->element->kind == TypeKind::UInt, "'umap' element must be UInt");

    // Ordered containers must NOT be marked unordered (the whole point of the canonical write order).
    static_assert(!has_flag(field("omap")->flags, FieldFlags::Unordered), "'omap' is ordered");
    static_assert(!has_flag(field("oset")->flags, FieldFlags::Unordered), "'oset' is ordered");
    static_assert(has_flag(field("uset")->flags, FieldFlags::Unordered), "'uset' must be marked unordered");

    // A container of containers is asserted again once that fixture member is back (the old
    // runtime cannot read such a member back — see the note in TestContainer.h).

    // Opaque elements are flagged; the plain member has no container facts at all (control).
    static_assert(!has_flag(field("plain")->flags, FieldFlags::Container), "'plain' is not a container");
    // A C array is a container by extent and its element description is ONE dimension down
    // (`int[2][3]` -> `int[3]` -> `int`), so the disk shape (extent, then per dimension) is expressed.
    static_assert(has_flag(field("grid")->flags, FieldFlags::Container), "'grid' must be a container");
    static_assert(field("grid")->element != nullptr, "'grid' must carry an element description");
    static_assert(field("grid")->element->kind == TypeKind::Array,
                  "'grid' element must be the array type `int[3]` (TypeKind::Array)");
    static_assert(field("grid")->element->element != nullptr &&
                  field("grid")->element->element->kind == TypeKind::Int,
                  "`int[3]`'s own element must be int");
    static_assert(field("plain")->key == nullptr && field("plain")->element == nullptr,
                  "'plain' must not carry element descriptions");

    // 与**新运行时**自己的容器判据同口径 —— 工具决定"这个成员是不是容器"，
    // 新运行时（`cacore::codegen::isContainer`：有 `size()` 与 `value_type`，或 C 数组按 extent）
    // 另外决定一次；两边不一致就会一边写容器、另一边当普通结构体写。
    // 夹具的每一个成员逐个断言，`plain` 与 `HasFloat` 是对照组（两边都必须为假）。
    template<typename T>
    constexpr bool parity(const char* name)
    {
        return has_flag(field(name)->flags, FieldFlags::Container) ==
               cacore::codegen::isContainer<T>::value;
    }

    static_assert(parity<std::vector<unsigned>>("vec"), "tool and runtime disagree about 'vec'");
    static_assert(parity<std::unordered_map<unsigned, unsigned>>("umap"),
                  "tool and runtime disagree about 'umap'");
    static_assert(parity<std::map<unsigned, unsigned>>("omap"), "tool and runtime disagree about 'omap'");
    static_assert(parity<std::set<unsigned>>("oset"), "tool and runtime disagree about 'oset'");
    static_assert(parity<std::unordered_set<unsigned>>("uset"),
                  "tool and runtime disagree about 'uset'");
    static_assert(parity<std::vector<std::vector<int>>>("nested"),
                  "tool and runtime disagree about 'nested'");
    static_assert(parity<std::vector<std::unordered_map<unsigned, unsigned>>>("vecOfUmap"),
                  "tool and runtime disagree about 'vecOfUmap'");
    static_assert(parity<std::unordered_map<unsigned, std::unordered_map<unsigned, unsigned>>>("umapOfUmap"),
                  "tool and runtime disagree about 'umapOfUmap'");
    static_assert(parity<std::unordered_map<unsigned, unsigned>[2]>("umaps"),
                  "tool and runtime disagree about 'umaps' (C array of containers)");
    static_assert(parity<std::unordered_map<unsigned, float>>("floatMap"),
                  "tool and runtime disagree about 'floatMap'");
    static_assert(parity<std::vector<codegen_containers::HasFloat>>("floats"),
                  "tool and runtime disagree about 'floats'");
    static_assert(parity<int[2][3]>("grid"), "tool and runtime disagree about 'grid'");
    static_assert(parity<std::string>("label"), "tool and runtime disagree about 'label'");
    static_assert(parity<unsigned>("plain"), "tool and runtime disagree about 'plain' (control)");
    // 对照组：元素类型本身不是容器（`Container` 标志只描述"这个成员是不是容器"）。
    static_assert(!cacore::codegen::isContainer<codegen_containers::HasFloat>::value,
                  "an element type must not be reported as a container");
}

// 撤除"元素是内含无序容器的结构体"这条拒绝之后，这个形状必须真的出码，
// 字段表也得说对 —— 元素是结构体（不是容器），它自己的成员才是无序容器。
namespace
{
    using I = codegen_containers::Inner;
    using O = codegen_containers::Outer;

    constexpr const CodeGenFieldInfo* outerField(const char* name)
    {
        for (std::size_t i = 0; i < TypeDescriptor<O>::fieldCount; ++i)
            if (TypeDescriptor<O>::fields()[i].name == name)
                return &TypeDescriptor<O>::fields()[i];
        return nullptr;
    }

    constexpr const CodeGenFieldInfo* innerField(const char* name)
    {
        for (std::size_t i = 0; i < TypeDescriptor<I>::fieldCount; ++i)
            if (TypeDescriptor<I>::fields()[i].name == name)
                return &TypeDescriptor<I>::fields()[i];
        return nullptr;
    }

    static_assert(TypeDescriptor<O>::fieldCount == 1, "Outer must have exactly one member");
    static_assert(has_flag(outerField("items")->flags, FieldFlags::Container),
                  "'items' must be a container");
    static_assert(!has_flag(outerField("items")->flags, FieldFlags::Unordered),
                  "'items' is an ordered vector");
    static_assert(outerField("items")->element != nullptr &&
                  outerField("items")->element->kind == TypeKind::Struct,
                  "'items' element must be the struct Inner");
    static_assert(cacore::codegen::isContainer<decltype(O{}.items)>::value,
                  "the runtime must agree that 'items' is a container");
    static_assert(cacore::codegen::isContainer<decltype(I{}.m)>::value &&
                  cacore::codegen::isUnorderedContainer<decltype(I{}.m)>::value,
                  "the runtime must agree that Inner::m is an unordered container");
    static_assert(cacore::codegen::hasGeneratedSerializer<O, std::vector<unsigned char>>::value,
                  "Outer must have generated serialization (this file includes Outer.Serialization.h)");
    static_assert(cacore::codegen::hasGeneratedSerializer<I, std::vector<unsigned char>>::value,
                  "Inner must have generated serialization (this file includes Inner.Serialization.h)");
}

// 定长数组（`castl::array` / C 数组）：在真实目标里**实例化**生成的读路径。
// 这一步比"出码成功"强：出码成功只说明工具跑通了，不说明这段代码能实例化 ——
// 一旦读路径退回去调用 `c.clear()`（`castl::array` 没有这个成员），下面两个实例化立刻编不过。
namespace
{
    using F = codegen_containers::FixedArrays;
    using FBytes = std::vector<unsigned char>;

    static_assert(cacore::codegen::isContainer<decltype(F{}.slots)>::value,
                  "'slots' 是定长数组，容器判据必须为真");
    static_assert(cacore::codegen::hasIndexer<decltype(F{}.slots)>::value,
                  "定长数组靠下标按位置覆写");
    static_assert(!cacore::codegen::hasClear<decltype(F{}.slots)>::value,
                  "castl::array 没有 clear()：这正是'读取路径不许假定有 clear()'的前提");
    static_assert(cacore::codegen::isContainer<decltype(F{}.grid)>::value,
                  "'grid' 是 C 数组，按 extent 算容器");
}

// 显式实例化必须写在全局作用域、且用完全限定的类型名（匿名名字空间里的别名会让它找不到成员）。
template bool cacore::codegen::CodeGenSerializer<codegen_containers::FixedArrays>::deserialize<
    std::vector<unsigned char>>(codegen_containers::FixedArrays&, std::vector<unsigned char> const&, std::size_t&);
template void cacore::codegen::CodeGenSerializer<codegen_containers::FixedArrays>::serialize<
    std::vector<unsigned char>>(codegen_containers::FixedArrays const&, std::vector<unsigned char>&);
