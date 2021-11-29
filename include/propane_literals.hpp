#ifndef _HEADER_PROPANE_LITERALS
#define _HEADER_PROPANE_LITERALS

#include "propane_common.hpp"

namespace propane
{
    union literal_t
    {
        constexpr literal_t() : as_u64(0) {}
        constexpr literal_t(int8_t i8) : as_i8(i8) {}
        constexpr literal_t(uint8_t u8) : as_u8(u8) {}
        constexpr literal_t(int16_t i16) : as_i16(i16) {}
        constexpr literal_t(uint16_t u16) : as_u16(u16) {}
        constexpr literal_t(int32_t i32) : as_i32(i32) {}
        constexpr literal_t(uint32_t u32) : as_u32(u32) {}
        constexpr literal_t(int64_t i64) : as_i64(i64) {}
        constexpr literal_t(uint64_t u64) : as_u64(u64) {}
        constexpr literal_t(float f32) : as_f32(f32) {}
        constexpr literal_t(double f64) : as_f64(f64) {}
        constexpr literal_t(void* vptr) : as_vptr(vptr) {}

        int8_t as_i8;
        uint8_t as_u8;
        int16_t as_i16;
        uint16_t as_u16;
        int32_t as_i32;
        uint32_t as_u32;
        int64_t as_i64;
        uint64_t as_u64;
        float as_f32;
        double as_f64;
        void* as_vptr;
    };
    static_assert(sizeof(literal_t) == sizeof(uint64_t), "Literal size invalid");

    template<typename value_t> struct parse_result
    {
        parse_result() = default;
        parse_result(type_idx type, value_t value) :
            type(type),
            value(value) {}

        type_idx type = type_idx::invalid;
        value_t value = value_t(0ull);

        inline bool is_valid() const noexcept
        {
            return type != type_idx::invalid;
        }
        inline operator bool() const noexcept
        {
            return is_valid();
        }
    };

    // Parse unsigned longs, which is the largest number representable in the current
    // environment. From ulong, it can be range-checked with anything smaller
    parse_result<uint64_t> parse_ulong(const char*& beg, const char* end, int32_t base);
    parse_result<uint64_t> parse_ulong(const char*& beg, const char* end);
    parse_result<uint64_t> parse_ulong(std::string_view str);

    // Parse the largest integer readable (ulong), and then determine integer type and negate if provided
    // Return value is an union with all possible data types
    parse_result<literal_t> parse_int_literal(const char*& beg, const char* end);
    parse_result<literal_t> parse_int_literal(std::string_view str);

    // Parse any literal (including floating point)
    parse_result<literal_t> parse_literal(const char*& beg, const char* end);
    parse_result<literal_t> parse_literal(std::string_view str);

    // Parse the largest number readable (ulong), and then apply integer type and negate if provided
    // Cast the result to the specified type
    template<typename value_t> concept literal_int = std::is_integral_v<value_t> || std::is_enum_v<value_t>;
    template<literal_int value_t> parse_result<value_t> parse_int_literal_cast(const char*& beg, const char* end)
    {
        parse_result<value_t> result;
        if (auto literal = parse_int_literal(beg, end))
        {
            switch (literal.type)
            {
                // First, cast to the encountered constant, then negate if applicable, then cast to the destination number type
                case type_idx::i8: { result.value = value_t(literal.value.as_i8); break; }
                case type_idx::u8: { result.value = value_t(literal.value.as_u8); break; }
                case type_idx::i16: { result.value = value_t(literal.value.as_i16); break; }
                case type_idx::u16: { result.value = value_t(literal.value.as_u16); break; }
                case type_idx::i32: { result.value = value_t(literal.value.as_i32); break; }
                case type_idx::u32: { result.value = value_t(literal.value.as_u32); break; }
                case type_idx::i64: { result.value = value_t(literal.value.as_i64); break; }
                case type_idx::u64: { result.value = value_t(literal.value.as_u64); break; }
                default: return result;
            }
            result.type = derive_type_index_v<value_t>;
        }
        return result;
    }
    template<literal_int value_t> parse_result<value_t> parse_int_literal_cast(std::string_view str)
    {
        const char* beg = str.data();
        const char* end = beg + str.size();
        return parse_int_literal_cast<value_t>(beg, end);
    }
}

#endif