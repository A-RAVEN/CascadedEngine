#pragma once
#include <type_traits>
#include <Hasher.h>
#include "CodeGenTraits.h"
#include <magic_enum/magic_enum.hpp>

template<typename T>
concept EnumConcept = std::is_enum_v<T>;


namespace uenum
{
	using namespace magic_enum;

	template <EnumConcept TEnumClass>
	struct TEnumTraits
	{
		static constexpr bool is_bitmask = false;
	};

	template <EnumConcept TEnumClass>
	struct TEnumTraitsInternal
	{
		using underlying_type = std::underlying_type_t<TEnumClass>;
		static constexpr underlying_type allFlags = ~0;
	};

	template<EnumConcept TEnumClass>
	static constexpr std::underlying_type_t<TEnumClass> enumToInt(TEnumClass e)
	{
		return static_cast<std::underlying_type_t<TEnumClass>>(e);
	}

	template<typename TEnumFlags, EnumConcept TEnumClass>
	static constexpr bool hasFlag(TEnumFlags flags, TEnumClass flag)
	{
		return static_cast<std::underlying_type_t<TEnumClass>>(flags & flag) != 0;
	}

	template <EnumConcept TEnumClass>
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
			return EnumFlags<TEnumClass>(m_mask ^ TEnumTraitsInternal<TEnumClass>::allFlags);
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

		constexpr int BitCount() const
		{
			return std::popcount(m_mask);
		}

		std::string to_string() const {
			return format_as(*this);
		}

		constexpr bool HasAll(EnumFlags<TEnumClass> const& test) const noexcept
		{
			return (m_mask & test.m_mask) == test.m_mask;
		}

		constexpr bool HasAny(EnumFlags<TEnumClass> const& test) const noexcept
		{
			return (m_mask & test.m_mask) != 0;
		}

	public:
		MaskType m_mask;
	};

}

// `EnumFlags` 是位掩码：它的值就是那一个成员 `m_mask`。这条包装规则就是它的读写规则 ——
// 写它包着的掩码，读回来也是这个掩码；字节与老的逐成员写法逐字节相同。
// 放在这里（宿主类型旁边）而不是运行时里：运行时不该认识引擎里的具体类型。
namespace cacore::codegen
{
	template<EnumConcept TEnumClass>
	struct wrapper_traits<uenum::EnumFlags<TEnumClass>>
	{
		static constexpr bool is_wrapper = true;
		using inner_type = typename uenum::EnumFlags<TEnumClass>::MaskType;

		static constexpr inner_type get_data(uenum::EnumFlags<TEnumClass> const& flags) noexcept
		{
			return flags.m_mask;
		}

		static constexpr void set_data(uenum::EnumFlags<TEnumClass>& flags, inner_type mask) noexcept
		{
			flags.m_mask = mask;
		}
	};
}

// bitwise operators
template <EnumConcept TEnumClass>
constexpr uenum::EnumFlags<TEnumClass> operator&(TEnumClass bit, uenum::EnumFlags<TEnumClass> const& flags) noexcept
{
	return flags.operator&(bit);
}

template <EnumConcept TEnumClass>
constexpr uenum::EnumFlags<TEnumClass> operator|(TEnumClass bit, uenum::EnumFlags<TEnumClass> const& flags) noexcept
{
	return flags.operator|(bit);
}

template <EnumConcept TEnumClass>
constexpr uenum::EnumFlags<TEnumClass> operator^(TEnumClass bit, uenum::EnumFlags<TEnumClass> const& flags) noexcept
{
	return flags.operator^(bit);
}

// bitwise operators on TEnumClass
template <EnumConcept TEnumClass, typename std::enable_if_t<uenum::TEnumTraits<TEnumClass>::is_bitmask> = true>
inline constexpr uenum::EnumFlags<TEnumClass> operator&(TEnumClass lhs, TEnumClass rhs) noexcept
{
	return uenum::EnumFlags<TEnumClass>(lhs) & rhs;
}

template <EnumConcept TEnumClass, typename std::enable_if<uenum::TEnumTraits<TEnumClass>::is_bitmask, bool>::type = true>
inline constexpr uenum::EnumFlags<TEnumClass> operator|(TEnumClass lhs, TEnumClass rhs) noexcept
{
	return uenum::EnumFlags<TEnumClass>(lhs) | rhs;
}

template <EnumConcept TEnumClass, typename std::enable_if<uenum::TEnumTraits<TEnumClass>::is_bitmask, bool>::type = true>
inline constexpr uenum::EnumFlags<TEnumClass> operator^(TEnumClass lhs, TEnumClass rhs) noexcept
{
	return uenum::EnumFlags<TEnumClass>(lhs) ^ rhs;
}

template <EnumConcept TEnumClass, typename std::enable_if<uenum::TEnumTraits<TEnumClass>::is_bitmask, bool>::type = true>
inline constexpr uenum::EnumFlags<TEnumClass> operator~(TEnumClass bit) noexcept
{
	return ~(uenum::EnumFlags<TEnumClass>(bit));
}

template <EnumConcept TEnumClass>
constexpr auto format_as(TEnumClass const& enumVal) {
	return uenum::enum_name(enumVal);
}

template<EnumConcept TEnumClass>
constexpr auto format_as(uenum::EnumFlags<TEnumClass> const& enumFlags) {
	using namespace magic_enum::bitwise_operators;
	std::string result;
	bool first = true;
	for (auto e : uenum::enum_values<TEnumClass>()) {
		if ((enumFlags & e) == e) {
			if (!first) {
				result += " | ";
			}
			result += std::string(uenum::enum_name(e));
			first = false;
		}
	}
	if (result.empty()) {
		result = "0";
	}
	return result;
}

#define CA_ENUM_FLAGS_NAMESPACE(EnumClass, NameSpace)\
namespace NameSpace { using EnumClass##Flags = uenum::EnumFlags<EnumClass>; }\
template<>struct ::uenum::TEnumTraits<NameSpace::EnumClass> { static constexpr bool is_bitmask = true;};

#define CA_ENUM_FLAGS(EnumClass)\
using EnumClass##Flags = uenum::EnumFlags<EnumClass>;\
template<>struct ::uenum::TEnumTraits<EnumClass> { static constexpr bool is_bitmask = true;};
