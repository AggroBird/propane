#ifndef _HEADER_LITERALS
#define _HEADER_LITERALS

#include "runtime.hpp"

namespace propane
{
    union literal_t
    {
        literal_t() : u64(0) {}
        literal_t(i8 i8) : i8(i8) {}
        literal_t(u8 u8) : u8(u8) {}
        literal_t(i16 i16) : i16(i16) {}
        literal_t(u16 u16) : u16(u16) {}
        literal_t(i32 i32) : i32(i32) {}
        literal_t(u32 u32) : u32(u32) {}
        literal_t(i64 i64) : i64(i64) {}
        literal_t(u64 u64) : u64(u64) {}
        literal_t(f32 f32) : f32(f32) {}
        literal_t(f64 f64) : f64(f64) {}
        literal_t(vptr vptr) : vptr(vptr) {}

        i8 i8;
        u8 u8;
        i16 i16;
        u16 u16;
        i32 i32;
        u32 u32;
        i64 i64;
        u64 u64;
        f32 f32;
        f64 f64;
        vptr vptr;
    };
    static_assert(sizeof(literal_t) == sizeof(uint64_t), "Literal size invalid");

    template<typename value_t> struct parse_result
    {
        parse_result() = default;
        parse_result(type_idx type, value_t value) :
            type(type),
            value(value) {}

        type_idx type = type_idx::invalid;
        value_t value = value_t(0);

        inline bool is_valid() const
        {
            return type != type_idx::invalid;
        }
        inline operator bool() const
        {
            return is_valid();
        }
    };

    template<typename value_t> constexpr value_t negate_num(value_t val, bool negate)
    {
        using signed_type = std::make_signed_t<value_t>;
        const signed_type negative = -static_cast<signed_type>(val);
        return negate ? static_cast<value_t>(negative) : val;
    }

    // '-'
    bool parse_negate(const char*& beg, const char* end);
    // '0x' or '0b'
    int32_t parse_integer_base(const char*& beg, const char* end);

    // Parse integer type from a suffix if provided (e.g. i32, u64, etc.)
    type_idx parse_integer_suffix(const char*& beg, const char* end);
    // Determine integer type from a suffix if provided (see above)
    // If no suffix is provided, try to guess the number type based on the biggest fit
    // int if value <= 2147483647, long if value <= 9223372036854775807, else ulong
    type_idx determine_integer_type(uint64_t value, const char*& beg, const char* end);

    // Parse unsigned longs, which is the largest number representable in the current
    // environment. From ulong, it can be range-checked with anything smaller
    parse_result<uint64_t> parse_ulong(const char*& beg, const char* end, int32_t base);
    inline parse_result<uint64_t> parse_ulong(const char*& beg, const char* end)
    {
        // Get base
        const int32_t base = parse_integer_base(beg, end);

        // Parse integer
        return parse_ulong(beg, end, base);
    }
    inline parse_result<uint64_t> parse_ulong(string_view str)
    {
        const char* beg = str.data();
        const char* end = beg + str.size();
        return parse_ulong(beg, end);
    }

    // Parse the largest number readable (ulong), and then apply integer type (see above) and negate if provided
    // Return value is an union with all possible data types
    parse_result<literal_t> parse_literal(const char*& beg, const char* end);
    inline parse_result<literal_t> parse_literal(string_view str)
    {
        const char* beg = str.data();
        const char* end = beg + str.size();
        return parse_literal(beg, end);
    }

    // Parse the largest number readable (ulong), and then apply integer type (see above) and negate if provided
    // Cast the result to the template type
    template<typename value_t> parse_result<value_t> parse_integer(const char*& beg, const char* end)
    {
        static_assert(std::is_integral_v<value_t> || std::is_enum_v<value_t>, "Type has to be integer");

        parse_result<value_t> result;
        if (auto literal = parse_literal(beg, end))
        {
            switch (literal.type)
            {
                // First, cast to the encountered constant, then negate if applicable, then cast to the destination number type
                case type_idx::i8: { result.value = value_t(literal.value.i8); break; }
                case type_idx::u8: { result.value = value_t(literal.value.u8); break; }
                case type_idx::i16: { result.value = value_t(literal.value.i16); break; }
                case type_idx::u16: { result.value = value_t(literal.value.u16); break; }
                case type_idx::i32: { result.value = value_t(literal.value.i32); break; }
                case type_idx::u32: { result.value = value_t(literal.value.u32); break; }
                case type_idx::i64: { result.value = value_t(literal.value.i64); break; }
                case type_idx::u64: { result.value = value_t(literal.value.u64); break; }
                default: return result;
            }
            result.type = derive_type_index_v<value_t>;
        }
        return result;
    }
    template<typename value_t> parse_result<value_t> parse_integer(string_view str)
    {
        const char* beg = str.data();
        const char* end = beg + str.size();
        return parse_integer<value_t>(beg, end);
    }
}

#endif