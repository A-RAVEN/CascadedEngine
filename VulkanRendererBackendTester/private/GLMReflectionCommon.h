#pragma once
#include <Reflection.h>
#include <glm/glm.hpp>

template<glm::length_t L, typename T, glm::qualifier Q>
struct careflection::containerInfo<glm::vec<L, T, Q>>
{
	using elementType = glm::vec<L, T, Q>::value_type;
	constexpr static auto container_size(const glm::vec<L, T, Q>& container)
	{
		return L;
	}
};

//template<length_t C, length_t R, typename T, qualifier Q = defaultp> struct mat

template<glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
struct careflection::containerInfo<glm::mat<C, R, T, Q>>
{
	using elementType = glm::mat<C, R, T, Q>::col_type;
	constexpr static auto container_size(const glm::mat<C, R, T, Q>& container)
	{
		return C;
	}
};