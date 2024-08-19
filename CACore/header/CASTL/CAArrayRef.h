#pragma once

namespace castl
{
    template <typename T>
    class array_ref
    {
    public:
        constexpr array_ref() noexcept
            : m_count(0)
            , m_ptr(nullptr)
        {
        }

        constexpr array_ref(std::nullptr_t) noexcept
            : m_count(0)
            , m_ptr(nullptr)
        {
        }

        array_ref(T const& value) noexcept
            : m_count(1)
            , m_ptr(&value)
        {
        }

        array_ref(size_t count, T const* ptr) noexcept
            : m_count(count)
            , m_ptr(ptr)
        {
        }

        template <std::size_t C>
        array_ref(T const (&ptr)[C]) noexcept
            : m_count(C)
            , m_ptr(ptr)
        {
        }

#  if __GNUC__ >= 9
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Winit-list-lifetime"
#  endif

        array_ref(std::initializer_list<T> const& list) noexcept
            : m_count(static_cast<size_t>(list.size()))
            , m_ptr(list.begin())
        {
        }

        template <typename B = T, typename std::enable_if<std::is_const<B>::value, int>::type = 0>
        array_ref(std::initializer_list<typename std::remove_const<T>::type> const& list) noexcept
            : m_count(static_cast<size_t>(list.size()))
            , m_ptr(list.begin())
        {
        }

#  if __GNUC__ >= 9
#    pragma GCC diagnostic pop
#  endif

        // Any type with a .data() return type implicitly convertible to T*, and a .size() return type implicitly
        // convertible to size_t. The const version can capture temporaries, with lifetime ending at end of statement.
        template <typename V,
            typename std::enable_if<std::is_convertible<decltype(std::declval<V>().data()), T*>::value&&
            std::is_convertible<decltype(std::declval<V>().size()), std::size_t>::value>::type* = nullptr>
        array_ref(V const& v) noexcept
            : m_count(static_cast<size_t>(v.size()))
            , m_ptr(v.data())
        {
        }

        const T* begin() const noexcept
        {
            return m_ptr;
        }

        const T* end() const noexcept
        {
            return m_ptr + m_count;
        }

        const T& front() const noexcept
        {
            VULKAN_HPP_ASSERT(m_count && m_ptr);
            return *m_ptr;
        }

        const T& back() const noexcept
        {
            VULKAN_HPP_ASSERT(m_count && m_ptr);
            return *(m_ptr + m_count - 1);
        }

        bool empty() const noexcept
        {
            return (m_count == 0);
        }

        size_t size() const noexcept
        {
            return m_count;
        }

        T const* data() const noexcept
        {
            return m_ptr;
        }

    private:
        size_t  m_count;
        T const* m_ptr;
    };
}