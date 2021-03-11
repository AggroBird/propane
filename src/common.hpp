#ifndef _HEADER_COMMON
#define _HEADER_COMMON

#include "propane_common.hpp"

#include <string>
using std::string;
#include <string_view>
using std::string_view;
#include <initializer_list>
using std::initializer_list;
#include <vector>
using std::vector;
#include <set>
using std::set;
#include <map>
using std::map;
#include <unordered_map>
using std::unordered_map;
#include <unordered_set>
using std::unordered_set;
#include <tuple>
using std::tuple;
#include <fstream>
using std::ifstream;
#include <sstream>
using std::stringstream;
#include <span>
using std::span;

#include <iostream>
#include <type_traits>
#include <utility>
#include <cstring>

#define CLASS_DEFAULT(type, copy, move, ...)    \
type(const type&) = copy;                       \
type& operator=(const type&) = copy;            \
type(type&&) noexcept = move;                   \
type& operator=(type&&) noexcept = move;        \
type(__VA_ARGS__)

#define NOCOPY_CLASS_DEFAULT(type, ...) CLASS_DEFAULT(type, delete, delete, __VA_ARGS__)
#define MOVABLE_CLASS_DEFAULT(type, ...) CLASS_DEFAULT(type, delete, default, __VA_ARGS__)

namespace propane
{
    // Fnv hash
    namespace fnv
    {
        constexpr platform_architecture architecture =
            sizeof(size_t) == 4 ? platform_architecture::x32 :
            sizeof(size_t) == 8 ? platform_architecture::x64 :
            platform_architecture::unknown;

        template<platform_architecture arch> constexpr size_t offset = 0;
        template<platform_architecture arch> constexpr size_t prime = 0;

        template<> constexpr size_t offset<platform_architecture::x32> = size_t(2166136261u);
        template<> constexpr size_t prime<platform_architecture::x32> = size_t(16777619u);

        template<> constexpr size_t offset<platform_architecture::x64> = size_t(14695981039346656037ull);
        template<> constexpr size_t prime<platform_architecture::x64> = size_t(1099511628211ull);


        inline size_t append(size_t hash, const char* const ptr, const size_t len) noexcept
        {
            for (size_t i = 0; i < len; ++i)
            {
                hash ^= static_cast<size_t>(static_cast<uint8_t>(ptr[i]));
                hash *= fnv::prime<fnv::architecture>;
            }

            return hash;
        }
        inline size_t hash(const char* const ptr, const size_t len) noexcept
        {
            return append(fnv::offset<fnv::architecture>, ptr, len);
        }

        // Template version
        template<typename value_t> inline size_t append(size_t hash, const value_t& val) noexcept
        {
            static_assert(std::is_trivial_v<value_t>, "Type must be trivial");
            return append(hash, reinterpret_cast<const char*>(&val), sizeof(value_t));
        }
        template<typename value_t> inline size_t hash(const value_t& val) noexcept
        {
            static_assert(std::is_trivial_v<value_t>, "Type must be trivial");
            return hash(reinterpret_cast<const char*>(&val), sizeof(value_t));
        }

        // String version
        template<size_t len> inline size_t append(size_t hash, const char(&str)[len]) noexcept
        {
            return append(hash, str, (len - 1));
        }
        template<size_t len> inline size_t hash(const char(&str)[len]) noexcept
        {
            return hash(str, (len - 1));
        }

        // Stringview version
        inline size_t append(size_t hash, string_view str) noexcept
        {
            return append(hash, str.data(), str.size());
        }
        inline size_t hash(string_view str) noexcept
        {
            return hash(str.data(), str.size());
        }

        // Void* version
        inline size_t append(size_t hash, const void* const ptr, const size_t len) noexcept
        {
            return append(hash, (const char* const)ptr, len);
        }
        inline size_t hash(const void* const ptr, const size_t len) noexcept
        {
            return hash((const char* const)ptr, len);
        }
    }

    // String format
    inline void format_recursive(stringstream& stream, const char* fmt, size_t len)
    {
        for (size_t i = 0; i < len; i++)
        {
            if (fmt[i] != '%') continue;
            stream << string_view(fmt, i) << '?';
            const size_t adv = (i + 1);
            return format_recursive(stream, fmt + adv, len - adv);
        }
        stream << string_view(fmt, len);
        return;
    }
    template<typename value_t, typename... args> inline void format_recursive(stringstream& stream, const char* fmt, size_t len, const value_t& val, const args&... arg)
    {
        for (size_t i = 0; i < len; i++)
        {
            if (fmt[i] != '%') continue;
            stream << string_view(fmt, i) << val;
            const size_t adv = (i + 1);
            return format_recursive(stream, fmt + adv, len - adv, arg...);
        }
        stream << string_view(fmt, len);
        return;
    }
    template<size_t len, typename... args> inline string format(const char(&fmt)[len], const args&... arg)
    {
        stringstream stream;
        format_recursive(stream, fmt, len - 1, arg...);
        return stream.str();
    }

    // Indexed vector
    template<typename key_t, typename value_t> class indexed_vector : public std::vector<value_t>
    {
    public:
        using std::vector<value_t>::vector;

        inline value_t& operator[](key_t idx) noexcept { return std::vector<value_t>::operator[](size_t(idx)); }
        inline const value_t& operator[](key_t idx) const noexcept { return vector<value_t>::operator[](size_t(idx)); }

        inline bool is_valid_index(key_t idx) const noexcept
        {
            return size_t(idx) < std::vector<value_t>::size();
        }
    };

    // Strip filepath
    inline const char* strip_filepath(const char* const path, size_t len) noexcept
    {
        const char* fp = path;
        for (const char* ptr = fp + len, *beg = path; ptr >= beg; ptr--)
        {
            if (*ptr == '\\' || *ptr == '/')
            {
                return ptr + 1;
            }
        }
        return path;
    }
    inline const char* strip_filepath(const char* const path) noexcept
    {
        return strip_filepath(path, strlen(path));
    }

    static constexpr bool check_size_range(uint64_t val) noexcept
    {
        return val <= uint64_t(~size_t(0));
    }

    // Tuple expansion into static fuction
    template <typename function, typename tuple_type, size_t... indices> static inline auto expand_sequence(function func, const tuple_type& tup, std::index_sequence<indices...>)
    {
        return func(std::get<indices>(tup)...);
    }
    template <typename function, typename tuple_type> static inline auto expand(function func, const tuple_type& tup)
    {
        return expand_sequence(func, tup, std::make_index_sequence<std::tuple_size_v<tuple_type>>{});
    }

    // Bitcount
    constexpr int32_t bitcount(uint64_t n) noexcept
    {
        int32_t count = 0;
        while (n)
        {
            n &= (n - 1);
            count++;
        }
        return count;
    }

    // Page size alignment
    constexpr size_t ceil_page_size(size_t len, size_t page_size) noexcept
    {
        return ((len + (page_size - 1)) / page_size) * page_size;
    }
}

#endif