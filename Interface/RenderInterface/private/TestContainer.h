#pragma once
// The container fixture. It lives inside an engine module
// (`Interface/RenderInterface`) on purpose — the owner directory decides the compile environment used
// to parse it, and the module-root environment is the one that can resolve the engine's own headers.
//
// The include set is deliberately small: `CASTL/CAContainerBase.h` (the `castl` namespace) plus the
// STL containers under test. Pulling in `CASTL/CAUnorderedMap.h` would drag `Hasher.h` → `CAHash.h` →
// `<sha256.h>`, which is NOT on every module's include path (measured: exit 6 under `Tools/CodeGen`).
#include "CodeGenMeta.h"
#include <CASTL/CAArray.h>
#include <CASTL/CAContainerBase.h>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace codegen_containers
{
    // An element with its own generated hash. `h.hash(element)` takes the trivially copyable
    // shortcut (raw bytes, so -0.0f and +0.0f differ); the element's own trait normalizes the float.
    CA_REFLECT(serializable)
    struct HasFloat
    {
        float f;
        friend bool operator==(const HasFloat& a, const HasFloat& b) { return a.f == b.f; }
    };

    // 撤除「元素是内含无序容器的结构体」这条拒绝所描述的形状 —— 容器里装结构体，
    // 而该结构体内部又有一个遍历顺序不确定的容器。撤除之前，对这个形状请求序列化必须是
    // `exit 12` 并在 stderr 里点名成员、元素类型与内层无序容器；撤除之后它必须出码、往返一致，
    // 且两个只有内层插入顺序不同的对象写出逐字节相同的流。
    //
    // `castl::unordered_map` 在这里不必额外带头文件：`CASTL/CAContainerBase.h` 已经把 `std` 引进
    // `castl` 名字空间（USING_EASTL=0 时），而这个夹具刻意不拉 `CASTL/CAUnorderedMap.h`
    // —— 那条路会带进 `Hasher.h` → `<sha256.h>`，本模块的编译环境里没有那个目录。
    CA_REFLECT(serializable)
    struct Inner
    {
        castl::unordered_map<unsigned, unsigned> m;
    };

    CA_REFLECT(serializable)
    struct Outer
    {
        castl::vector<Inner> items;
    };

    // 定长数组：`castl::array`（== `std::array`）与 C 数组都没有 `clear()`，也没有插入接口。
    // 生成的读取路径必须按位置逐个覆写，并在读之前校验载荷长度与 extent 一致 ——
    // 一旦它退回去调用 `c.clear()`，在真实目标里实例化这条读路径就会编译失败。
    CA_REFLECT(serializable)
    struct FixedArrays
    {
        castl::array<int, 4> slots = {};     // 一维定长数组
        int                  grid[2][2] = {}; // C 数组（多维，按维度嵌套写）
    };

    CA_REFLECT(serializable)
    struct Containers
    {
        std::vector<unsigned>                  vec;      // ordered, value elements
        std::unordered_map<unsigned, unsigned> umap;     // unordered, key + value
        std::map<unsigned, unsigned>           omap;     // ordered map
        std::set<unsigned>                     oset;     // ordered set
        std::unordered_set<unsigned>           uset;     // unordered set
        std::vector<std::vector<int>>          nested;   // ordered container of containers
        std::vector<std::unordered_map<unsigned, unsigned>> vecOfUmap;   // nested unordered
        std::unordered_map<unsigned, std::unordered_map<unsigned, unsigned>> umapOfUmap;
        std::unordered_map<unsigned, unsigned> umaps[2];                 // C array of containers
        std::unordered_map<unsigned, float>    floatMap;                 // float normalization
        std::vector<HasFloat>                  floats;                  // element trait
        signed int                             grid[2][3]; // C array: element description is `int[3]`
        std::string                            label;    // ordered container
        unsigned                               plain;    // not a container (control)
    };
}
