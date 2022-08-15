#ifndef _HEADER_PROPANE_SPAN
#define _HEADER_PROPANE_SPAN

#if defined(__cpp_lib_span)

#include <span>

#else

namespace std
{
    // Alternative for span when when lacking C++20.
    // Supports most collection types used in propane toolchain,
    // assuming they define data() and size() methods
    template<typename value_t> class span
    {
    public:
        inline span() : ptr(nullptr), len(0)
        {

        }
        inline span(value_t* ptr, size_t len) : ptr(ptr), len(len)
        {

        }

        template<size_t length> inline span(value_t(&arr)[length])
        {
            ptr = arr;
            len = length;
        }
        template<typename other_t> span(other_t& other)
        {
            ptr = other.data();
            len = other.size();
        }

        span(const span&) = default;
        span& operator=(const span&) = default;


        inline value_t* begin()
        {
            return ptr;
        }
        inline value_t* end()
        {
            return ptr + len;
        }
        inline const value_t* begin() const
        {
            return ptr;
        }
        inline const value_t* end() const
        {
            return ptr + len;
        }

        inline value_t& operator[](size_t idx)
        {
            return ptr[idx];
        }
        inline const value_t& operator[](size_t idx) const
        {
            return ptr[idx];
        }

        inline value_t* data()
        {
            return ptr;
        }
        inline const value_t* data() const
        {
            return ptr;
        }

        inline size_t size() const
        {
            return len;
        }
        inline bool empty() const
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