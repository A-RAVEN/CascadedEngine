#pragma once
#include "CAContainerBase.h"
#include <Serialization.h>
#if USING_EASTL
#include <EASTL/shared_ptr.h>
#else
#include <memory>
#endif

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
