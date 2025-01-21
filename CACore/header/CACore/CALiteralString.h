#pragma once
#include <CASTL/CAString.h>

namespace castl
{
    template<size_t n>
    struct string_literal
    {
        constexpr string_literal(const char(&str)[n]) { std::copy_n(str, n, value); };
        char value[n];

        //constexpr string_literal(const char(&str)[n]) : value(str){ };
        //const char* value;

        constexpr static size_t count = n;

        template <size_t index>
        consteval char get_char() const {
            return value[index < n ? index : n - 1];
        }
    };
}