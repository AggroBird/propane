#ifndef _HEADER_PROPANE_SPAN
#define _HEADER_PROPANE_SPAN

#if defined(__cpp_lib_span)

#include <span>

namespace propane
{
    template<typename value_t, size_t extend = std::dynamic_extent>
    using span = std::span<value_t, extend>;
}

#else

#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)

#include <array>

namespace propane
{
    // Simple alternative for span when when lacking C++20.
    // Supports most collection types used in propane toolchain,
    // assuming they define data() and size() methods.
    template<typename value_t> class span
    {
    public:
        inline constexpr span() noexcept :
            ptr(nullptr), len(0)
        {

        }
        inline constexpr span(value_t* ptr, size_t len)  noexcept :
            ptr(ptr), len(len)
        {
            
        }

        template<size_t length> inline constexpr span(value_t(&arr)[length]) noexcept :
            ptr(arr), len(length)
        {

        }
        template<typename other_t> constexpr span(other_t& other) noexcept :
            ptr(std::data(other)), len(std::size(other))
        {

        }

        constexpr span(const span&) noexcept = default;
        constexpr span& operator=(const span&) noexcept = default;


        inline constexpr value_t* begin() noexcept
        {
            return ptr;
        }
        inline constexpr value_t* end() noexcept
        {
            return ptr + len;
        }
        inline constexpr const value_t* begin() const noexcept
        {
            return ptr;
        }
        inline constexpr const value_t* end() const noexcept
        {
            return ptr + len;
        }

        inline constexpr value_t& operator[](size_t idx) noexcept
        {
            return ptr[idx];
        }
        inline constexpr const value_t& operator[](size_t idx) const noexcept
        {
            return ptr[idx];
        }

        inline constexpr value_t* data() noexcept
        {
            return ptr;
        }
        inline constexpr const value_t* data() const noexcept
        {
            return ptr;
        }

        inline constexpr size_t size() const noexcept
        {
            return len;
        }
        inline constexpr bool empty() const noexcept
        {
            return len == 0;
        }

    private:
        value_t* ptr;
        size_t len;
    };
}

#endif

#endif

#endif