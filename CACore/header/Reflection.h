#pragma once
#include "DebugUtils.h"
#include <type_traits>

namespace careflection
{
    struct any_type
    {
        template<typename T>
        constexpr operator T() const { return T{}; }
    };

    template <class T>
    concept is_c_array = (std::is_bounded_array_v<std::remove_cvref_t<T>> && std::extent_v<std::remove_cvref_t<T>> > 0);

    template<typename T, typename Enable = void>
    struct containerInfo;

    template<typename T>
    concept is_size_container = requires(T t)
    {
        t.size();
        typename T::value_type;
    };

    template<typename T>
    concept has_foreach_loop = is_c_array<T> || requires(T t)
    {
        t.begin();
        t.end();
    };

    template<typename T>
    concept has_resize = requires(T t)
    {
        t.resize(0);
    };

    template<typename T>
    concept has_reserve = requires(T t)
    {
        t.reserve(0);
    };

    template<typename T>
    concept element_assignable = requires(T t)
    {
        t[0] = (typename containerInfo<T>::elementType) any_type{};
    };

    template<typename T>
    concept has_push_back_element = requires(T t)
    {
        t.push_back((typename containerInfo<T>::elementType) any_type {});
    };

    template<typename T>
    concept has_insert_element = requires(T t)
    {
        t.insert((typename containerInfo<T>::elementType) any_type {});
    };

    template<typename T>
    concept has_data = requires(T t)
    {
        t.data();
    };

    template<typename T>
    concept is_bytebuffer = is_size_container<T> && has_resize<T> && has_data<T> && requires(T t)
    {
        sizeof(typename T::value_type) == 1;
    };

    template<typename T>
    concept is_byte_source = is_size_container<T> && has_data<T> && requires(T t)
    {
        sizeof(typename T::value_type) == 1;
    };

    template<typename T>
    concept is_tuple_like = requires(T t)
	{
		std::tuple_size<T>::value;
		std::tuple_element_t<0, T>{};
	};

    template<typename T>
    struct containerStates
    {
        using noRefT = std::remove_reference_t<T>;
        constexpr static bool is_c_array = is_c_array<noRefT>;
        constexpr static bool is_size_container = is_size_container<noRefT>;
        constexpr static bool has_resize = has_resize<noRefT>;
        constexpr static bool has_data = has_data<noRefT>;
        constexpr static bool has_reserve = has_reserve<noRefT>;
        constexpr static bool element_assignable = element_assignable<noRefT>;
        constexpr static bool has_push_back_element = has_push_back_element<noRefT>;
        constexpr static bool has_insert_element = has_insert_element<noRefT>;
        constexpr static bool is_bytebuffer = is_bytebuffer<noRefT>;
        constexpr static bool is_byte_source = is_byte_source<noRefT>;
        constexpr static bool has_foreach_loop = has_foreach_loop<noRefT>;
        constexpr static bool is_container_with_size = is_size_container || is_c_array;
    };

    template<typename T, typename Enable = void>
    struct containerInfo
    {
        using elementType = T;
        constexpr static auto container_size(const T& container)
        {
            return 0;
        }
    };

    template<typename T>
    struct containerInfo<T, std::enable_if_t<is_size_container<T>>>
    {
        using elementType = std::remove_const_t<typename T::value_type>;
        constexpr static auto container_size(const T& container)
        {
            return container.size();
        }
    };

    template<typename T>
    struct containerInfo<T, std::enable_if_t<is_c_array<T>>>
    {
        using elementType = std::remove_all_extents<T>::type;
        constexpr static auto container_size(const T& container)
        {
            return std::extent_v<std::remove_cvref_t<T>>;
        }
    };

    template<class T>
    struct structured_binding_assert {
        static_assert(std::is_aggregate_v<T> || is_tuple_like<T>, "Type T is not aggregate type.");
    };


    template<typename T, typename... Args>
    consteval size_t aggregate_member_count()
    {
        structured_binding_assert<T> assert{};
        if constexpr(is_tuple_like<T>)
		{
			return std::tuple_size<T>::value;
		}
        //判断用args...和一个任意类型能否构造出T,如果不能说明args为全部的参数,返回参数个数
        else if constexpr (requires {T{ {Args{}}..., {any_type{}} }; } == false)
        {
            return sizeof...(Args);
        }
        else
        {
            return aggregate_member_count<T, Args..., any_type>();
        }
    }

    template<typename Obj, typename Visitor>
    constexpr decltype(auto) inline visit_members_structured_binding(Obj&& object, Visitor&& visitor);

    constexpr decltype(auto) inline visit_members(auto&& object, auto&& visitor)
    {
        using objType = std::remove_reference_t<decltype(object)>;
        using visitorType = std::remove_reference_t<decltype(visitor)>;
        return visit_members_structured_binding(object, visitor);
    }

    constexpr static auto max_visit_members = 64;
    template<typename Obj, typename Visitor>
    constexpr decltype(auto) inline visit_members_structured_binding(Obj&& object, Visitor&& visitor)
    {
        using objType = std::remove_reference_t<decltype(object)>;
        using visitorType = std::remove_reference_t<decltype(visitor)>;
        constexpr auto member_count = aggregate_member_count<objType>();

        static_assert(member_count < max_visit_members, "struct exceeds max member count, whitch is 64");
#pragma region Boilerplate
        if constexpr (member_count == 0) {
            return visitor();
        }
        else if constexpr (member_count == 1) {
            auto&& [a1] = object;
            return visitor(a1);
        }
        else if constexpr (member_count == 2) {
            auto&& [a1, a2] = object;
            return visitor(a1, a2);
        }
        else if constexpr (member_count == 3) {
            auto&& [a1, a2, a3] = object;
            return visitor(a1, a2, a3);
        }
        else if constexpr (member_count == 4) {
            auto&& [a1, a2, a3, a4] = object;
            return visitor(a1, a2, a3, a4);
        }
        else if constexpr (member_count == 5) {
            auto&& [a1, a2, a3, a4, a5] = object;
            return visitor(a1, a2, a3, a4, a5);
        }
        else if constexpr (member_count == 6) {
            auto&& [a1, a2, a3, a4, a5, a6] = object;
            return visitor(a1, a2, a3, a4, a5, a6);
        }
        else if constexpr (member_count == 7) {
            auto&& [a1, a2, a3, a4, a5, a6, a7] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7);
        }
        else if constexpr (member_count == 8) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8);
        }
        else if constexpr (member_count == 9) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9);
        }
        else if constexpr (member_count == 10) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
        }
        else if constexpr (member_count == 11) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
        }
        else if constexpr (member_count == 12) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
        }
        else if constexpr (member_count == 13) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
        }
        else if constexpr (member_count == 14) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14] =
                object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14);
        }
        else if constexpr (member_count == 15) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14,
                a15] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15);
        }
        else if constexpr (member_count == 16) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16);
        }
        else if constexpr (member_count == 17) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17);
        }
        else if constexpr (member_count == 18) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18);
        }
        else if constexpr (member_count == 19) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19);
        }
        else if constexpr (member_count == 20) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20);
        }
        else if constexpr (member_count == 21) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21);
        }
        else if constexpr (member_count == 22) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22);
        }
        else if constexpr (member_count == 23) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23);
        }
        else if constexpr (member_count == 24) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24);
        }
        else if constexpr (member_count == 25) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
                a25);
        }
        else if constexpr (member_count == 26) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26);
        }
        else if constexpr (member_count == 27) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27] =
                object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27);
        }
        else if constexpr (member_count == 28) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28] =
                object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28);
        }
        else if constexpr (member_count == 29) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29);
        }
        else if constexpr (member_count == 30) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30);
        }
        else if constexpr (member_count == 31) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31);
        }
        else if constexpr (member_count == 32) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32);
        }
        else if constexpr (member_count == 33) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33);
        }
        else if constexpr (member_count == 34) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34);
        }
        else if constexpr (member_count == 35) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35);
        }
        else if constexpr (member_count == 36) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36);
        }
        else if constexpr (member_count == 37) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36,
                a37);
        }
        else if constexpr (member_count == 38) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38);
        }
        else if constexpr (member_count == 39) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39);
        }
        else if constexpr (member_count == 40) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40] =
                object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40);
        }
        else if constexpr (member_count == 41) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41] =
                object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41);
        }
        else if constexpr (member_count == 42) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42);
        }
        else if constexpr (member_count == 43) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43);
        }
        else if constexpr (member_count == 44) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44);
        }
        else if constexpr (member_count == 45) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45);
        }
        else if constexpr (member_count == 46) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46);
        }
        else if constexpr (member_count == 47) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47);
        }
        else if constexpr (member_count == 48) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48);
        }
        else if constexpr (member_count == 49) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48,
                a49);
        }
        else if constexpr (member_count == 50) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49, a50] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49,
                a50);
        }
        else if constexpr (member_count == 51) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49, a50, a51] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49,
                a50, a51);
        }
        else if constexpr (member_count == 52) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49,
                a50, a51, a52);
        }
        else if constexpr (member_count == 53) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53] =
                object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49,
                a50, a51, a52, a53);
        }
        else if constexpr (member_count == 54) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54] =
                object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49,
                a50, a51, a52, a53, a54);
        }
        else if constexpr (member_count == 55) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54,
                a55] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49,
                a50, a51, a52, a53, a54, a55);
        }
        else if constexpr (member_count == 56) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54,
                a55, a56] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49,
                a50, a51, a52, a53, a54, a55, a56);
        }
        else if constexpr (member_count == 57) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54,
                a55, a56, a57] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49,
                a50, a51, a52, a53, a54, a55, a56, a57);
        }
        else if constexpr (member_count == 58) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54,
                a55, a56, a57, a58] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49,
                a50, a51, a52, a53, a54, a55, a56, a57, a58);
        }
        else if constexpr (member_count == 59) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54,
                a55, a56, a57, a58, a59] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49,
                a50, a51, a52, a53, a54, a55, a56, a57, a58, a59);
        }
        else if constexpr (member_count == 60) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54,
                a55, a56, a57, a58, a59, a60] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49,
                a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60);
        }
        else if constexpr (member_count == 61) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54,
                a55, a56, a57, a58, a59, a60, a61] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49,
                a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60,
                a61);
        }
        else if constexpr (member_count == 62) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54,
                a55, a56, a57, a58, a59, a60, a61, a62] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49,
                a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61,
                a62);
        }
        else if constexpr (member_count == 63) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54,
                a55, a56, a57, a58, a59, a60, a61, a62, a63] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49,
                a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61,
                a62, a63);
        }
        else if constexpr (member_count == 64) {
            auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,
                a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28,
                a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41,
                a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54,
                a55, a56, a57, a58, a59, a60, a61, a62, a63, a64] = object;
            return visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13,
                a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,
                a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37,
                a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49,
                a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61,
                a62, a63, a64);
        }
#pragma endregion
    }

}