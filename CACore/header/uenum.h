#pragma once
#include <type_traits>
#include <Hasher.h>

namespace uenum
{
    template <typename TEnumClass>
    struct TEnumTraits
    {
       static constexpr bool is_bitmask = false;
       static constexpr std::underlying_type_t<TEnumClass> EnumToInt(TEnumClass e) { return static_cast<std::underlying_type_t<TEnumClass>>(e); })
    };

    template<typename TEnumClass>
    static constexpr std::underlying_type_t<TEnumClass> enumToInt(TEnumClass e)
    {
        return static_cast<std::underlying_type_t<TEnumClass>>(e);
    }

    template <typename TEnumClass>
    class EnumFlags
    {
    public:
        using MaskType = typename std::underlying_type<TEnumClass>::type;

        // constructors
        constexpr EnumFlags() noexcept : m_mask(0) {}

        constexpr EnumFlags(TEnumClass bit) noexcept : m_mask(static_cast<MaskType>(bit)) {}

        constexpr EnumFlags(EnumFlags<TEnumClass> const& rhs) noexcept = default;

        //constexpr explicit EnumFlags(MaskType flags) noexcept : m_mask(flags) {}
        constexpr EnumFlags(MaskType flags) noexcept : m_mask(flags) {}


        auto operator<=>(EnumFlags<TEnumClass> const&) const = default;

//        // relational operators
//#if defined( VULKAN_HPP_HAS_SPACESHIP_OPERATOR )
//        auto operator<=>(EnumFlags<TEnumClass> const&) const = default;
//#else
//        constexpr bool operator<(EnumFlags<TEnumClass> const& rhs) const noexcept
//        {
//            return m_mask < rhs.m_mask;
//        }
//
//        constexpr bool operator<=(EnumFlags<TEnumClass> const& rhs) const noexcept
//        {
//            return m_mask <= rhs.m_mask;
//        }
//
//        constexpr bool operator>(EnumFlags<TEnumClass> const& rhs) const noexcept
//        {
//            return m_mask > rhs.m_mask;
//        }
//
//        constexpr bool operator>=(EnumFlags<TEnumClass> const& rhs) const noexcept
//        {
//            return m_mask >= rhs.m_mask;
//        }
//
//        constexpr bool operator==(EnumFlags<TEnumClass> const& rhs) const noexcept
//        {
//            return m_mask == rhs.m_mask;
//        }
//
//        constexpr bool operator!=(EnumFlags<TEnumClass> const& rhs) const noexcept
//        {
//            return m_mask != rhs.m_mask;
//        }
//#endif

        // logical operator
        constexpr bool operator!() const noexcept
        {
            return !m_mask;
        }

        // bitwise operators
        constexpr EnumFlags<TEnumClass> operator&(EnumFlags<TEnumClass> const& rhs) const noexcept
        {
            return EnumFlags<TEnumClass>(m_mask & rhs.m_mask);
        }

        constexpr EnumFlags<TEnumClass> operator|(EnumFlags<TEnumClass> const& rhs) const noexcept
        {
            return EnumFlags<TEnumClass>(m_mask | rhs.m_mask);
        }

        constexpr EnumFlags<TEnumClass> operator^(EnumFlags<TEnumClass> const& rhs) const noexcept
        {
            return EnumFlags<TEnumClass>(m_mask ^ rhs.m_mask);
        }

        constexpr EnumFlags<TEnumClass> operator~() const noexcept
        {
            return EnumFlags<TEnumClass>(m_mask ^ TEnumTraits<TEnumClass>::allFlags);
        }

        // assignment operators
        constexpr EnumFlags<TEnumClass>& operator=(EnumFlags<TEnumClass> const& rhs) noexcept = default;

        constexpr EnumFlags<TEnumClass>& operator|=(EnumFlags<TEnumClass> const& rhs) noexcept
        {
            m_mask |= rhs.m_mask;
            return *this;
        }

        constexpr EnumFlags<TEnumClass>& operator&=(EnumFlags<TEnumClass> const& rhs) noexcept
        {
            m_mask &= rhs.m_mask;
            return *this;
        }

        constexpr EnumFlags<TEnumClass>& operator^=(EnumFlags<TEnumClass> const& rhs) noexcept
        {
            m_mask ^= rhs.m_mask;
            return *this;
        }

        // cast operators
        explicit constexpr operator bool() const noexcept
        {
            return !!m_mask;
        }

        explicit constexpr operator MaskType() const noexcept
        {
            return m_mask;
        }

    public:
        MaskType m_mask;
    };

}
template<typename TEnumClass>
CA_REFLECTION_TEMPLATE(uenum::EnumFlags<TEnumClass>, m_mask);

//
//#if !defined( VULKAN_HPP_HAS_SPACESHIP_OPERATOR )
//// relational operators only needed for pre C++20
//template <typename TEnumClass>
//constexpr bool operator<(TEnumClass bit, uenum::EnumFlags<TEnumClass> const& flags) noexcept
//{
//    return flags.operator>(bit);
//}
//
//template <typename TEnumClass>
//constexpr bool operator<=(TEnumClass bit, uenum::EnumFlags<TEnumClass> const& flags) noexcept
//{
//    return flags.operator>=(bit);
//}
//
//template <typename TEnumClass>
//constexpr bool operator>(TEnumClass bit, uenum::EnumFlags<TEnumClass> const& flags) noexcept
//{
//    return flags.operator<(bit);
//}
//
//template <typename TEnumClass>
//constexpr bool operator>=(TEnumClass bit, uenum::EnumFlags<TEnumClass> const& flags) noexcept
//{
//    return flags.operator<=(bit);
//}
//
//template <typename TEnumClass>
//constexpr bool operator==(TEnumClass bit, uenum::EnumFlags<TEnumClass> const& flags) noexcept
//{
//    return flags.operator==(bit);
//}
//
//template <typename TEnumClass>
//constexpr bool operator!=(TEnumClass bit, uenum::EnumFlags<TEnumClass> const& flags) noexcept
//{
//    return flags.operator!=(bit);
//}
//#endif

// bitwise operators
template <typename TEnumClass>
constexpr uenum::EnumFlags<TEnumClass> operator&(TEnumClass bit, uenum::EnumFlags<TEnumClass> const& flags) noexcept
{
    return flags.operator&(bit);
}

template <typename TEnumClass>
constexpr uenum::EnumFlags<TEnumClass> operator|(TEnumClass bit, uenum::EnumFlags<TEnumClass> const& flags) noexcept
{
    return flags.operator|(bit);
}

template <typename TEnumClass>
constexpr uenum::EnumFlags<TEnumClass> operator^(TEnumClass bit, uenum::EnumFlags<TEnumClass> const& flags) noexcept
{
    return flags.operator^(bit);
}

// bitwise operators on TEnumClass
template <typename TEnumClass, typename std::enable_if_t<uenum::TEnumTraits<TEnumClass>::is_bitmask> = true>
inline constexpr uenum::EnumFlags<TEnumClass> operator&(TEnumClass lhs, TEnumClass rhs) noexcept
{
    return uenum::EnumFlags<TEnumClass>(lhs) & rhs;
}

template <typename TEnumClass, typename std::enable_if<uenum::TEnumTraits<TEnumClass>::is_bitmask, bool>::type = true>
inline constexpr uenum::EnumFlags<TEnumClass> operator|(TEnumClass lhs, TEnumClass rhs) noexcept
{
    return uenum::EnumFlags<TEnumClass>(lhs) | rhs;
}

template <typename TEnumClass, typename std::enable_if<uenum::TEnumTraits<TEnumClass>::is_bitmask, bool>::type = true>
inline constexpr uenum::EnumFlags<TEnumClass> operator^(TEnumClass lhs, TEnumClass rhs) noexcept
{
    return uenum::EnumFlags<TEnumClass>(lhs) ^ rhs;
}

template <typename TEnumClass, typename std::enable_if<uenum::TEnumTraits<TEnumClass>::is_bitmask, bool>::type = true>
inline constexpr uenum::EnumFlags<TEnumClass> operator~(TEnumClass bit) noexcept
{
    return ~(uenum::EnumFlags<TEnumClass>(bit));
}