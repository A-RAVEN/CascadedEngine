#pragma once
#include "CAContainerBase.h"
#if USING_EASTL
#include <EASTL/shared_ptr.h>
#else
#include <memory>
#endif
#include <CodeGenTraits.h>

// shared_ptr 的句柄规则（新运行时的口径）：它是个"句柄"，哈希按它持有的地址算。
// 旧侧那份"把 `shared_ptr` 成员当托管指针"的特化，原先只为让含 `shared_ptr` 成员的老式夹具过旧反射的
// `object incompatible for reflection` 断言；那批夹具（旧运行时自己的用例）已经删掉，实测删掉这份特化
// 之后整个工程只剩那个夹具文件编不过，所以它随夹具一起去掉了。
namespace cacore::codegen
{
    template<typename T>
    struct pointer_traits<castl::shared_ptr<T>, void>
    {
        static constexpr bool is_pointer_handle = true;
        using pointer_type = castl::shared_ptr<T>;
        static T const* get_pointer(pointer_type const& ptr) { return ptr.get(); }
        static void set_pointer_null(pointer_type& ptr) { ptr = nullptr; }
    };
}
