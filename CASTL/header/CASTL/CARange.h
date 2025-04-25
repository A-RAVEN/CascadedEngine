#pragma once
#include "CAAlgorithm.h"
#include "CATypeTraits.h"
namespace castl
{
	//inclusive head and exclusive tail
	template<typename T>
	class range
	{
		static_assert(castl::is_integral_v<T>, "castl::range<T>: T must be numeric type");
	public:
		range() : m_head(), m_tail() {}
		range(T const& head, T const& tail) : m_head(head), m_tail(tail) {}
		T const& head() const { return m_head; }
		T const& tail() const { return m_tail; }
		T& head() { return m_head; }
		T& tail() { return m_tail; }
		T size() const { return m_tail - m_head; }

		bool overlaps(range const& other) const
		{
			return m_head < other.tail() && m_tail > other.head();
		}
		bool connect(range const& other) const
		{
			return m_tail == other.head() || other.tail() == m_head;
		}
		bool can_combine(range const& other) const
		{
			return overlaps(other) || connect(other);
		}

		void encapsule(T const& value)
		{
			m_head = castl::min(m_head, value);
			m_tail = castl::max(m_tail, value + 1);
		}

		void expand(range const& other)
		{
			m_head = castl::min(m_head, other.head());
			m_tail = castl::max(m_tail, other.tail());
		}

		static range invalid()
		{
			return range(0, 0);
		}
	private:
		T m_head;
		T m_tail;
	};
}