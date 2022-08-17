#ifndef _HEADER_PARSER_TOKENS
#define _HEADER_PARSER_TOKENS

#include "common.hpp"

namespace propane
{
    enum class token_type : uint32_t
    {
        // Keywords
        kw_global,
        kw_constant,
        kw_method,
        kw_struct,
        kw_union,
        kw_stack,
        kw_returns,
        kw_parameters,
        kw_init,
        kw_end,
        kw_null,

        // Instructions
        op_noop,

        op_set,
        op_conv,

        op_not,
        op_neg,
        op_mul,
        op_div,
        op_mod,
        op_add,
        op_sub,
        op_lsh,
        op_rsh,
        op_and,
        op_xor,
        op_or,

        op_padd,
        op_psub,
        op_pdif,

        op_cmp,
        op_ceq,
        op_cne,
        op_cgt,
        op_cge,
        op_clt,
        op_cle,
        op_cze,
        op_cnz,

        op_br,
        op_beq,
        op_bne,
        op_bgt,
        op_bge,
        op_blt,
        op_ble,
        op_bze,
        op_bnz,

        op_sw,

        op_call,
        op_callv,
        op_ret,
        op_retv,

        op_dump,

        // Special characters
        lbrace,
        rbrace,
        lbracket,
        rbracket,
        lparen,
        rparen,
        deref,
        asterisk,
        ampersand,
        exclamation,
        circumflex,
        colon,
        comma,
        period,

        identifier,
        literal,

        eof,
        invalid,
    };

    struct token_string
    {
        constexpr token_string() : str(), type(token_type::invalid) {}
        constexpr token_string(string_view str, token_type type) : str(str), type(type) {}

        string_view str;
        token_type type;
    };

    static constexpr token_string token_strings[] =
    {
        { "global", token_type::kw_global },
        { "constant", token_type::kw_constant },
        { "method", token_type::kw_method },
        { "struct", token_type::kw_struct },
        { "union", token_type::kw_union },
        { "stack", token_type::kw_stack },
        { "returns", token_type::kw_returns },
        { "parameters", token_type::kw_parameters },
        { "init", token_type::kw_init },
        { "end", token_type::kw_end },
        { "null", token_type::kw_null },

        { "noop", token_type::op_noop },
        { "set", token_type::op_set },
        { "conv", token_type::op_conv },
        { "not", token_type::op_not },
        { "neg", token_type::op_neg },
        { "mul", token_type::op_mul },
        { "div", token_type::op_div },
        { "mod", token_type::op_mod },
        { "add", token_type::op_add },
        { "sub", token_type::op_sub },
        { "lsh", token_type::op_lsh },
        { "rsh", token_type::op_rsh },
        { "and", token_type::op_and },
        { "xor", token_type::op_xor },
        { "or", token_type::op_or },
        { "padd", token_type::op_padd },
        { "psub", token_type::op_psub },
        { "pdif", token_type::op_pdif },
        { "cmp", token_type::op_cmp },
        { "ceq", token_type::op_ceq },
        { "cne", token_type::op_cne },
        { "cgt", token_type::op_cgt },
        { "cge", token_type::op_cge },
        { "clt", token_type::op_clt },
        { "cle", token_type::op_cle },
        { "cze", token_type::op_cze },
        { "cnz", token_type::op_cnz },
        { "br", token_type::op_br },
        { "beq", token_type::op_beq },
        { "bne", token_type::op_bne },
        { "bgt", token_type::op_bgt },
        { "bge", token_type::op_bge },
        { "blt", token_type::op_blt },
        { "ble", token_type::op_ble },
        { "bze", token_type::op_bze },
        { "bnz", token_type::op_bnz },
        { "sw", token_type::op_sw },
        { "call", token_type::op_call },
        { "callv", token_type::op_callv },
        { "ret", token_type::op_ret },
        { "retv", token_type::op_retv },
        { "dump", token_type::op_dump },
    };
    constexpr size_t token_string_count = sizeof(token_strings) / sizeof(token_string);

    struct token_string_lookup_table_t
    {
        static constexpr size_t letter_count = ('z' - 'a') + 1;

        struct lookup_range
        {
            constexpr lookup_range() : beg(0), end(0) {}
            constexpr lookup_range(size_t beg, size_t end) : beg(beg), end(end) {}

            size_t beg;
            size_t end;
        };

        static constexpr int32_t compare(string_view lhs, string_view rhs)
        {
            size_t min = lhs.size() < rhs.size() ? lhs.size() : rhs.size();
            for (size_t i = 0; i < min; i++)
            {
                if (lhs[i] < rhs[i]) return -1;
                if (lhs[i] > rhs[i]) return 1;
            }
            if (lhs.size() < rhs.size()) return -1;
            if (lhs.size() > rhs.size()) return 1;
            return 0;
        }

        static constexpr token_string_lookup_table_t make_lookup_table(std::span<const token_string> strings)
        {
            token_string_lookup_table_t result = {};
            int32_t count = 0;
            for (auto& it : strings)
            {
                bool added = false;
                for (int32_t i = 0; i < count; i++)
                {
                    if (compare(it.str, result.strings[i].str) < 1)
                    {
                        for (int32_t j = count; j > i; j--)
                        {
                            result.strings[j] = result.strings[j - 1];
                        }
                        result.strings[i] = it;
                        count++;
                        added = true;
                        break;
                    }
                }
                if (!added)
                {
                    result.strings[count] = it;
                    count++;
                }
            }
            for (int32_t i = 0; i < count; i++)
            {
                size_t index = result.strings[i].str[0] - 'a';
                auto& range = result.ranges[index];
                if (range.end == 0)
                {
                    range.beg = i;
                    range.end = i + 1;
                }
                else
                {
                    range.end++;
                }
            }
            return result;
        }

        constexpr token_string try_find_token(string_view str) const
        {
            if (str.size() > 0 && str[0] >= 'a' && str[0] <= 'z')
            {
                const char c = str[0];
                if (c >= 'a' && c <= 'z')
                {
                    const auto& range = ranges[c - 'a'];
                    for (size_t i = range.beg; i < range.end; i++)
                    {
                        if (strings[i].str == str)
                        {
                            return strings[i];
                        }
                    }
                }
            }

            return token_string(string_view(), token_type::invalid);
        }

        lookup_range ranges[letter_count];
        token_string strings[token_string_count];
    };

    constexpr token_string_lookup_table_t token_string_lookup_table = token_string_lookup_table_t::make_lookup_table(token_strings);
}

#endif