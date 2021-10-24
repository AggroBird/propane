#include "propane_literals.hpp"
#include "common.hpp"


namespace propane
{
    inline constexpr bool is_bit(char c) noexcept
    {
        return c == '0' || c == '1';
    }
    inline constexpr bool is_uppercase_hex(char c) noexcept
    {
        return c >= 'A' && c <= 'F';
    }
    inline constexpr bool is_lowercase_hex(char c) noexcept
    {
        return c >= 'a' && c <= 'f';
    }
    inline constexpr bool is_digit(char c) noexcept
    {
        return c >= '0' && c <= '9';
    }
    inline constexpr char to_lower(char c)
    {
        return (c >= 'A' && c <= 'Z') ? (c - 'A') + 'a' : c;
    }

    template<typename value_t> constexpr value_t negate_num(value_t val, bool negate)
    {
        return negate ? static_cast<value_t>(-static_cast<std::make_signed_t<value_t>>(val)) : val;
    }

    template<size_t len> inline bool cmp_str(const char*& beg, const char* end, const char(&str)[len])
    {
        const auto offset = size_t(end - beg);
        if (offset == (len - 1))
        {
            for (size_t i = 0; i < offset; i++)
            {
                const char c = to_lower(beg[i]);
                if (c != str[i])
                {
                    return false;
                }
            }
            beg += offset;
            return true;
        }
        return false;
    }


    // '-'
    bool parse_negate(const char*& beg, const char* end)
    {
        const auto offset = end - beg;
        if (offset > 1)
        {
            if (beg[0] == '-')
            {
                beg++;
                return true;
            }
            else if (beg[0] == '+')
            {
                beg++;
                return false;
            }
        }
        return false;
    }

    // '0x' or '0b'
    int32_t parse_base(const char*& beg, const char* end)
    {
        int32_t base = 10;
        const auto offset = end - beg;
        if (offset >= 2 && beg[0] == '0')
        {
            if (beg[1] == 'x' || beg[1] == 'X')
            {
                base = 16;
                beg += 2;
            }
            else if (beg[1] == 'b' || beg[1] == 'B')
            {
                base = 2;
                beg += 2;
            }
        }
        return base;
    }

    // Parse integer type from a suffix if provided (e.g. i32, u64, etc.)
    type_idx parse_integer_suffix(const char*& beg, const char* end)
    {
        type_idx btype = type_idx::invalid;
        if (beg < end)
        {
            // Manual type override
            switch (beg[0])
            {
                case 'i':
                case 'I':
                {
                    if (cmp_str(beg, end, "i8")) return type_idx::i8;
                    if (cmp_str(beg, end, "i16")) return type_idx::i16;
                    if (cmp_str(beg, end, "i32")) return type_idx::i32;
                    if (cmp_str(beg, end, "i64")) return type_idx::i64;
                }
                break;

                case 'u':
                case 'U':
                {
                    if (cmp_str(beg, end, "u8")) return type_idx::u8;
                    if (cmp_str(beg, end, "u16")) return type_idx::u16;
                    if (cmp_str(beg, end, "u32")) return type_idx::u32;
                    if (cmp_str(beg, end, "u64")) return type_idx::u64;
                    if (cmp_str(beg, end, "u")) return type_idx::u32;
                    if (cmp_str(beg, end, "ul")) return type_idx::u64;
                }
                break;

                case 'l':
                case 'L':
                {
                    if (cmp_str(beg, end, "l")) return type_idx::i64;
                }
                break;
            }
        }
        return btype;
    }

    // Determine integer type from a suffix if provided (see above)
    // If no suffix is provided, try to guess the number type based on the biggest fit
    // int if value <= 2147483647, long if value <= 9223372036854775807, else ulong
    type_idx determine_integer_type(uint64_t value, const char*& beg, const char* end)
    {
        type_idx btype = parse_integer_suffix(beg, end);
        if (btype == type_idx::invalid)
        {
            if (value <= uint64_t(std::numeric_limits<int32_t>::max())) btype = type_idx::i32;
            else if (value <= uint64_t(std::numeric_limits<int64_t>::max())) btype = type_idx::i64;
            else btype = type_idx::u64;
        }
        return btype;
    }


    parse_result<uint64_t> parse_ulong(const char*& beg, const char* end, int32_t base)
    {
        parse_result<uint64_t> result;
        if (beg >= end) return result;

        uint64_t value = 0;

        // Find end
        for (const char* ptr = beg; ptr < end; ptr++)
        {
            const char c = *ptr;
            if (base == 16 && (is_lowercase_hex(c) || is_uppercase_hex(c))) continue;
            if (base >= 10 && is_digit(c)) continue;
            if (base == 2 && is_bit(c)) continue;
            end = ptr;
            break;
        }
        if (beg >= end) return result;

        const size_t len = size_t(end - beg);
        if (base == 16)
        {
            // Check overflow
            if (len > 16) return result;

            uint64_t mul = 1;
            const char* ptr = end - 1;
            for (size_t i = 0; i < len; i++, ptr--)
            {
                const char c = *ptr;
                if (is_digit(c)) value += (uint64_t(c) - uint64_t('0')) * mul;
                else if (is_lowercase_hex(c)) value += ((uint64_t(c) - uint64_t('a')) + 10) * mul;
                else if (is_uppercase_hex(c)) value += ((uint64_t(c) - uint64_t('A')) + 10) * mul;
                mul *= 16;
            }
        }
        else if (base == 10)
        {
            constexpr string_view num_max = string_view("18446744073709551615");

            // Check overflow
            if (len > num_max.size()) return result;
            if (len == num_max.size())
            {
                for (size_t i = 0; i < len; i++)
                {
                    if (beg[i] > num_max[i]) return result;
                    if (beg[i] < num_max[i]) break;
                }
            }

            uint64_t mul = 1;
            const char* ptr = end - 1;
            for (size_t i = 0; i < len; i++, ptr--)
            {
                const char c = *ptr;
                value += (uint64_t(c) - uint64_t('0')) * mul;
                mul *= 10;
            }
        }
        else if (base == 2)
        {
            // Check overflow
            if (len > 64) return result;

            uint64_t mul = 1;
            const char* ptr = end - 1;
            for (size_t i = 0; i < len; i++, ptr--)
            {
                const char c = *ptr;
                value += (uint64_t(c) - uint64_t('0')) * mul;
                mul *= 2;
            }
        }
        else
        {
            return result;
        }

        beg += len;
        result.type = type_idx::u64;
        result.value = value;

        return result;
    }
    parse_result<uint64_t> parse_ulong(const char*& beg, const char* end)
    {
        // Get base
        const int32_t base = parse_base(beg, end);

        // Parse integer
        return parse_ulong(beg, end, base);
    }
    parse_result<uint64_t> parse_ulong(string_view str)
    {
        const char* beg = str.data();
        const char* end = beg + str.size();
        return parse_ulong(beg, end);
    }

    parse_result<literal_t> parse_int_literal(const char*& beg, const char* end, bool negate, int32_t base)
    {
        parse_result<literal_t> result;
        if (parse_result<uint64_t> as_ulong = parse_ulong(beg, end, base))
        {
            type_idx btype = determine_integer_type(as_ulong.value, beg, end);
            if (beg != end) return result;

            uint64_t i = as_ulong.value;
            switch (btype)
            {
                case type_idx::i8: result = parse_result<literal_t>(btype, negate_num(i8(i), negate)); break;
                case type_idx::u8: result = parse_result<literal_t>(btype, negate_num(u8(i), negate)); break;
                case type_idx::i16: result = parse_result<literal_t>(btype, negate_num(i16(i), negate)); break;
                case type_idx::u16: result = parse_result<literal_t>(btype, negate_num(u16(i), negate)); break;
                case type_idx::i32: result = parse_result<literal_t>(btype, negate_num(i32(i), negate)); break;
                case type_idx::u32: result = parse_result<literal_t>(btype, negate_num(u32(i), negate)); break;
                case type_idx::i64: result = parse_result<literal_t>(btype, negate_num(i64(i), negate)); break;
                case type_idx::u64: result = parse_result<literal_t>(btype, negate_num(u64(i), negate)); break;
            }
        }
        return result;
    }
    parse_result<literal_t> parse_int_literal(const char*& beg, const char* end)
    {
        const bool negate = parse_negate(beg, end);
        const int32_t base = parse_base(beg, end);
        return parse_int_literal(beg, end, negate, base);
    }
    parse_result<literal_t> parse_int_literal(string_view str)
    {
        const char* beg = str.data();
        const char* end = beg + str.size();
        return parse_int_literal(beg, end);
    }

    parse_result<literal_t> parse_literal(const char*& beg, const char* end)
    {
        parse_result<literal_t> result;

        const bool negate = parse_negate(beg, end);
        const int32_t base = parse_base(beg, end);

        if (beg >= end) return result;

        if (base == 10)
        {
            bool is_float = false;
            bool is_exp = false;
            for (const char* ptr = beg; ptr < end; ptr++)
            {
                const char c = *ptr;
                if (c == '.')
                {
                    // Only 1 period
                    if (is_float) return result;
                    is_float = true;
                }
                else if (c == 'e' || c == 'E')
                {
                    // Exponent already set
                    if (is_exp) return result;
                    is_exp = is_float = true;
                }
                else if (c == 'f' || c == 'F')
                {
                    is_float = true;
                    break;
                }
            }

            if (is_float)
            {
                // Only actual numbers
                if (!is_digit(*beg)) return result;

                char* next = nullptr;
                const double d = strtod(beg, &next);
                if (!isfinite(d)) return result;
                if (next == beg || next == nullptr) return result;

                beg = next;

                type_idx btype = type_idx::f64;

                // Parse suffix
                if (beg < end)
                {
                    switch (beg[0])
                    {
                        case 'f':
                        case 'F':
                        {
                            if (cmp_str(beg, end, "f")) { btype = type_idx::f32; break; }
                            if (cmp_str(beg, end, "f32")) { btype = type_idx::f32; break; }
                            if (cmp_str(beg, end, "f64")) { btype = type_idx::f64; break; }
                        }
                        break;
                    }
                }

                switch (btype)
                {
                    case type_idx::f32: result = parse_result<literal_t>(type_idx::f32, float(negate ? -d : d)); break;
                    case type_idx::f64: result = parse_result<literal_t>(type_idx::f64, negate ? -d : d); break;
                }

                return result;
            }
        }

        // Return regular number
        return parse_int_literal(beg, end, negate, base);
    }
    parse_result<literal_t> parse_literal(string_view str)
    {
        const char* beg = str.data();
        const char* end = beg + str.size();
        return parse_literal(beg, end);
    }
}