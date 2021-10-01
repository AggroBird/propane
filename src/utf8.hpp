#ifndef _HEADER_UTF8
#define _HEADER_UTF8

#include <cstdint>

namespace propane
{
    // Utf8 decoder
    // Decode a codepoint from bytes
    // Guaranteed to fail if the byte sequence does not yield a value within a valid codepoint range
    class utf8_decoder
    {
    public:
        utf8_decoder();

        // Add a byte to the decoder
        // When return value is true, out_codepoint will contain codepoint value
        bool decode(uint8_t in_byte, uint32_t& out_codepoint) noexcept;

        // Returns false if the state is corrupt
        // Continued decoding in a corrupt state will result in undefined behavior
        operator bool() const noexcept;

    private:
        uint32_t state;
        uint32_t codepoint;
    };

    namespace utf8
    {
        // Encode a codepoint to bytes
        // This guarantees to encode as long as the codepoint is <= 0x10FFFF, regardless of validitiy
        bool encode(uint32_t codepoint, uint8_t out_bytes[4], size_t& out_len);

        // Returns true if valid ascii
        constexpr bool is_ascii(uint32_t codepoint) noexcept
        {
            return codepoint <= 0x7Fu;
        }
        constexpr bool is_ascii(char chr) noexcept
        {
            return chr >= 0 && is_ascii(uint32_t(chr));
        }

        // Returns true if valid utf8
        constexpr bool is_utf8(uint32_t codepoint) noexcept
        {
            if (codepoint <= 0x10FFFFu)
            {
                if (is_ascii(codepoint))
                {
                    // plain ASCII
                    return true;
                }

                if (codepoint <= 0x7FFu)
                {
                    // 2-byte unicode
                    return codepoint >= 0x80u;
                }

                if (codepoint <= 0xFFFFu)
                {
                    // 3-byte unicode
                    return
                        (codepoint >= 0x800u && codepoint <= 0xFFFu) ||
                        (codepoint >= 0x1000u && codepoint <= 0xCFFFu) ||
                        (codepoint >= 0xD000u && codepoint <= 0xD7FFu) ||
                        (codepoint >= 0xE000u);
                }

                // 4-byte unicode
                return
                    (codepoint >= 0x10000u && codepoint <= 0x3FFFFu) ||
                    (codepoint >= 0x40000u && codepoint <= 0xFFFFFu) ||
                    (codepoint >= 0x100000u);
            }

            return false;
        }
    }
}

#endif