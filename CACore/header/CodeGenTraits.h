#pragma once
//
// CodeGenTraits.h — 新运行时的两条「宿主自己给特化」判据。
//
// 它们单独放一个头，是为了让**宿主类型**（例如 `uenum::EnumFlags<T>`）能只为一件事
// 引入这份轻量声明，而不必把整个序列化运行时（容器、缓冲区那一套）拖进它的头文件。
//
// 包装类型：写的是它包着的东西，不是它的内存表示。
//   特化要提供：`is_wrapper` / `inner_type` / `get_data(obj)` / `set_data(obj, inner)`。
// 句柄类型：哈希按它持有的指针地址算。
//   特化要提供：`is_pointer_handle` / `get_pointer(obj)` / `set_pointer_null(obj)`。

#include <type_traits>

namespace cacore::codegen
{
    template<typename T, typename = void>
    struct wrapper_traits
    {
        static constexpr bool is_wrapper = false;
    };

    template<typename T, typename = void>
    struct pointer_traits
    {
        static constexpr bool is_pointer_handle = false;
    };
}
