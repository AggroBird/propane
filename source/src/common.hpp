#ifndef _HEADER_COMMON
#define _HEADER_COMMON

#include "propane_common.hpp"

using std::span;
using std::string;
using std::string_view;
using std::initializer_list;

#include <array>
using std::array;
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

#include <iostream>
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
    // These are commonly used in switch tabes
    // because they are all (almost) equal length
    typedef int8_t i8;
    typedef uint8_t u8;
    typedef int16_t i16;
    typedef uint16_t u16;
    typedef int32_t i32;
    typedef uint32_t u32;
    typedef int64_t i64;
    typedef uint64_t u64;
    typedef float f32;
    typedef double f64;
    typedef void* vptr;

    // Fnv hash
    namespace fnv
    {
        constexpr platform_architecture architecture =
            sizeof(size_t) == 4 ? platform_architecture::x32 :
            sizeof(size_t) == 8 ? platform_architecture::x64 :
            platform_architecture::unknown;

        template<platform_architecture arch> constexpr size_t offset = 0;
        template<platform_architecture arch> constexpr size_t prime = 0;

        template<> constexpr size_t offset<platform_architecture::x32> = static_cast<size_t>(2166136261u);
        template<> constexpr size_t prime<platform_architecture::x32> = static_cast<size_t>(16777619u);

        template<> constexpr size_t offset<platform_architecture::x64> = static_cast<size_t>(14695981039346656037ull);
        template<> constexpr size_t prime<platform_architecture::x64> = static_cast<size_t>(1099511628211ull);


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

        // CString version
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

        // String version
        inline size_t append(size_t hash, string str) noexcept
        {
            return append(hash, str.data(), str.size());
        }
        inline size_t hash(string str) noexcept
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

        inline value_t& operator[](key_t idx) noexcept { return std::vector<value_t>::operator[](static_cast<size_t>(idx)); }
        inline const value_t& operator[](key_t idx) const noexcept { return vector<value_t>::operator[](static_cast<size_t>(idx)); }

        inline bool is_valid_index(key_t idx) const noexcept
        {
            return static_cast<size_t>(idx) < std::vector<value_t>::size();
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

    // Ensure value within range of size_t (size_t is smaller in 32 bit)
    static constexpr bool check_size_range(uint64_t val) noexcept
    {
        return val <= static_cast<uint64_t>(~static_cast<size_t>(0));
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

    // Read/write bytes
    template<typename src_t> inline const src_t& read(const uint8_t* ptr) noexcept
    {
        static_assert(std::is_trivial_v<src_t>, "Type must be trivial");
        const src_t& src = *reinterpret_cast<const src_t*>(ptr);
        return src;
    }
    template<typename dst_t> inline dst_t& write(uint8_t* ptr) noexcept
    {
        static_assert(std::is_trivial_v<dst_t>, "Type must be trivial");
        dst_t& dst = *reinterpret_cast<dst_t*>(ptr);
        return dst;
    }

    template<typename key_t, typename value_t> inline bool is_valid_index(const vector<value_t>& vec, key_t idx) noexcept
    {
        return static_cast<size_t>(idx) < vec.size();
    }

    // String writer
    class string_writer_base : public string
    {
    public:
        inline void write(const char* c, size_t len)
        {
            insert(end(), c, c + len);
        }
        template<size_t len> inline void write(const char(&str)[len])
        {
            write(str, len - 1);
        }
        inline void write(string_view str)
        {
            insert(end(), str.begin(), str.end());
        }
        inline void write(const char c)
        {
            push_back(c);
        }

        inline void write_space()
        {
            write(' ');
        }
        inline void write_tab()
        {
            write('\t');
        }
        inline void write_newline()
        {
            write('\n');
        }
    };
    class string_writer : public string_writer_base
    {
    public:
        inline void write()
        {

        }
        template<typename value_t, typename... args_t> inline void write(const value_t& val, const args_t&... arg)
        {
            string_writer_base::write(val);
            write(arg...);
        }

        template<typename value_t> inline string_writer& operator<<(const value_t& val)
        {
            string_writer_base::write(val);
            return *this;
        }
    };

    struct address_info
    {
        inline address_info(address_header header) noexcept
        {
            index = header.index();
            modifier = header.modifier();
            prefix = header.prefix();
            type = header.type();
        }

        uint32_t index;
        address_modifier modifier;
        address_prefix prefix;
        address_type type;
    };

    class number_converter
    {
    public:
        number_converter(size_t precision = 17)
        {
            num_converter.precision(std::streamsize(precision));
        }

        template<typename value_t> const string& convert(value_t val)
        {
            constexpr size_t buffer_size = 1024;
            num_converter.str(string());
            num_converter << val;
            num_buffer.resize(buffer_size);
            num_converter.get(num_buffer.data(), std::streamsize(buffer_size));
            num_buffer.resize(static_cast<size_t>(num_converter.gcount()));
            num_converter.seekg(0, num_converter.beg);
            return num_buffer;
        }

    private:
        stringstream num_converter;
        string num_buffer;
    };

    static constexpr size_t approximate_handle_size(size_t class_size)
    {
        return (size_t)(class_size / sizeof(size_t));
    }
}

#endif