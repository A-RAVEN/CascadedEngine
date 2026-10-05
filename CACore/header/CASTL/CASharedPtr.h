#pragma once
#include "CAContainerBase.h"
#if USING_EASTL
#include <EASTL/shared_ptr.h>
#else
#include <memory>
#endif
#include <CodeGenTraits.h>
#include <Reflection.h>   // 过渡期：下面那份旧侧 managed_pointer_traits 特化要用它的主模板   // 新运行时的句柄判据主模板（下面那段特化要用）

// 新运行时的句柄规则：shared_ptr 是一个"句柄"，哈希按它持有的地址算（与旧运行时同口径）。
// 旧侧那份 careflection::managed_pointer_traits 已随旧头退场，这一份是新侧的对应物。
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

// 过渡期：**旧运行时（`visit_members` 那条路）还需要这一份** —— 含 `shared_ptr` 成员的老式类型走老路算哈希时，
// 就靠它把成员当"托管指针"处理。删掉它实测会让 `Reflection.h(350): 'object incompatible for reflection'` 变红。
// 新侧对应物是上面那份 `cacore::codegen::pointer_traits`；等 9.3 删三份旧头时，这一份随老路一起删。
namespace careflection
{
    template<typename T>
    struct managed_pointer_traits<castl::shared_ptr<T>>
    {
        constexpr static bool is_managed_pointer = true;
        using pointer_type = castl::shared_ptr<T>;
        using pointee_type = T;
        constexpr static T const* get_pointer(pointer_type const& ptr) { return ptr.get(); }
        constexpr static T* get_pointer(pointer_type& ptr) { return ptr.get(); }
        constexpr static void set_pointer_null(pointer_type& ptr) { ptr = nullptr; }
    };
}