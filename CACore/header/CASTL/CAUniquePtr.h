#pragma once
#include "CAContainerBase.h"
#if USING_EASTL
#include <EASTL/unique_ptr.h>
#else
#include <memory>
#endif
#include <CodeGenTraits.h>

// unique_ptr 的句柄规则（新运行时的口径）：它是个"句柄"，哈希按它持有的地址算。
// 旧侧那份"把 `unique_ptr` 成员当托管指针"的特化与 `shared_ptr` 那份同因同果，随旧运行时的夹具一起删掉
// （实测删掉之后整个工程只剩那个夹具文件编不过）。
namespace cacore::codegen
{
    template<typename T>
    struct pointer_traits<castl::unique_ptr<T>, void>
    {
        static constexpr bool is_pointer_handle = true;
        using pointer_type = castl::unique_ptr<T>;
        static T const* get_pointer(pointer_type const& ptr) { return ptr.get(); }
        static void set_pointer_null(pointer_type& ptr) { ptr = nullptr; }
    };
}
