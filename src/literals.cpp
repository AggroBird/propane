#include "literals.hpp"


namespace propane
{
    template<size_t len> inline bool cmp_str(const char*& beg, const char* end, const char(&str)[len])
    {
        const auto offset = end - beg;
        if (offset == (len - 1) && memcmp(str, beg, offset) == 0)
        {
            beg += (len - 1);
            return true;
        }
        return false;
    }


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

    type_idx parse_integer_suffix(const char*& beg, const char* end)
    {
        type_idx btype = type_idx::invalid;
        if (beg < end)
        {
            // Manual type override
            if (cmp_str(beg, end, "i8")) btype = type_idx::i8;
            else if (cmp_str(beg, end, "u8")) btype = type_idx::u8;
            else if (cmp_str(beg, end, "i16")) btype = type_idx::i16;
            else if (cmp_str(beg, end, "u16")) btype = type_idx::u16;
            else if (cmp_str(beg, end, "i32")) btype = type_idx::i32;
            else if (cmp_str(beg, end, "u32")) btype = type_idx::u32;
            else if (cmp_str(beg, end, "i64")) btype = type_idx::i64;
            else if (cmp_str(beg, end, "u64")) btype = type_idx::u64;
        }
        return btype;
    }

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
            if (base == 16 && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) continue;
            if (base >= 10 && (c >= '0' && c <= '9')) continue;
            if (base == 2 && (c == '0' || c == '1')) continue;
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
                if (c >= '0' && c <= '9') value += (uint64_t(c) - uint64_t('0')) * mul;
                else if (c >= 'a' && c <= 'f') value += ((uint64_t(c) - uint64_t('a')) + 10) * mul;
                else if (c >= 'A' && c <= 'F') value += ((uint64_t(c) - uint64_t('A')) + 10) * mul;
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

    parse_result<literal_t> parse_int_literal(const char*& beg, const char* end)
    {
        // Get negate
        const bool negate = parse_negate(beg, end);

        // Get base
        const int32_t base = parse_base(beg, end);

        parse_result<literal_t> result;
        if (parse_result<uint64_t> as_ulong = parse_ulong(beg, end, base))
        {
            type_idx btype = determine_integer_type(as_ulong.value, beg, end);
            if (beg != end) return result;

            uint64_t i = as_ulong.value;
            switch (btype)
            {
                case type_idx::i8: return parse_result<literal_t>(btype, negate_num(i8(i), negate));
                case type_idx::u8: return parse_result<literal_t>(btype, negate_num(u8(i), negate));
                case type_idx::i16: return parse_result<literal_t>(btype, negate_num(i16(i), negate));
                case type_idx::u16: return parse_result<literal_t>(btype, negate_num(u16(i), negate));
                case type_idx::i32: return parse_result<literal_t>(btype, negate_num(i32(i), negate));
                case type_idx::u32: return parse_result<literal_t>(btype, negate_num(u32(i), negate));
                case type_idx::i64: return parse_result<literal_t>(btype, negate_num(i64(i), negate));
                case type_idx::u64: return parse_result<literal_t>(btype, negate_num(u64(i), negate));
            }
        }
        return result;
    }
}