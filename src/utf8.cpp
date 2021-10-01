#include "utf8.hpp"


namespace propane
{
    namespace utf8
    {
        // Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
        // http://bjoern.hoehrmann.de/utf-8/decoder/dfa/

        constexpr uint32_t accept = 0;
        constexpr uint32_t reject = 1;

        constexpr uint8_t utf8d[] =
        {
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 00..1f
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 20..3f
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 40..5f
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 60..7f
            1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, // 80..9f
            7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, // a0..bf
            8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // c0..df
            0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, // e0..ef
            0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, // f0..ff
            0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, // s0..s0
            1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, // s1..s2
            1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, // s3..s4
            1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, // s5..s6
            1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // s7..s8
        };
    }


    utf8_decoder::utf8_decoder() : state(utf8::accept), codepoint(0u)
    {

    }

    bool utf8_decoder::decode(uint8_t in_byte, uint32_t& out_codepoint) noexcept
    {
        const uint32_t type = utf8::utf8d[in_byte];
        codepoint = (state != utf8::accept) ? ((in_byte & 0x3Fu) | (codepoint << 6)) : ((0xFFu >> type) & (in_byte));
        state = utf8::utf8d[256u + state * 16u + type];

        out_codepoint = codepoint;
        return state == utf8::accept;
    }

    utf8_decoder::operator bool() const noexcept
    {
        return state != utf8::reject;
    }


    bool utf8::encode(uint32_t codepoint, uint8_t out_bytes[4], size_t& out_len)
    {
        if (codepoint <= 0x10FFFFu)
        {
            if (codepoint <= 0x7Fu)
            {
                // plain ASCII
                out_bytes[0] = (uint8_t)codepoint;
                out_len = 1;
                return true;
            }

            if (codepoint <= 0x7FFu)
            {
                // 2-byte unicode
                out_bytes[0] = (uint8_t)((codepoint + (0b110u << 11)) >> 6);
                out_bytes[1] = (uint8_t)((codepoint & 0x3Fu) + 0x80u);
                out_len = 2;
                return true;
            }

            if (codepoint <= 0xFFFFu)
            {
                // 3-byte unicode
                out_bytes[0] = (uint8_t)((codepoint + (0b1110u << 16)) >> 12);
                out_bytes[1] = (uint8_t)(((codepoint & (0x3Fu << 6)) >> 6) + 0x80u);
                out_bytes[2] = (uint8_t)((codepoint & 0x3Fu) + 0x80u);
                out_len = 3;
                return true;
            }

            // 4-byte unicode
            out_bytes[0] = (uint8_t)((codepoint + (0b11110u << 21)) >> 18);
            out_bytes[1] = (uint8_t)(((codepoint & (0x3Fu << 12)) >> 12) + 0x80u);
            out_bytes[2] = (uint8_t)(((codepoint & (0x3Fu << 6)) >> 6) + 0x80u);
            out_bytes[3] = (uint8_t)((codepoint & 0x3Fu) + 0x80u);
            out_len = 4;
            return true;
        }

        out_len = 0;
        return false;
    }
}
