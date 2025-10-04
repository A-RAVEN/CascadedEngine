#pragma once
#include <type_traits>
#include <Hasher.h>
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

	public:
		MaskType m_mask;
	};

}
template<EnumConcept TEnumClass>
CA_REFLECTION_TEMPLATE(uenum::EnumFlags<TEnumClass>, m_mask);

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
