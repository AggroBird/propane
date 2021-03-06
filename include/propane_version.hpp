#ifndef _HEADER_PROPANE_VERSION
#define _HEADER_PROPANE_VERSION

#include <cinttypes>

namespace propane
{
    enum class platform_endianness : uint8_t
    {
        unknown = 0,
        little,
        big,
        little_word,
        big_word,
    };

    enum class platform_architecture : uint8_t
    {
        unknown = 0,
        x32,
        x64,
    };

    struct toolchain_version
    {
        toolchain_version();
        toolchain_version(uint16_t major, uint16_t minor, uint32_t changelist, platform_endianness endianness, platform_architecture architecture);

        uint16_t major() const noexcept;
        uint16_t minor() const noexcept;
        uint32_t changelist() const noexcept;

        platform_endianness endianness() const noexcept;
        platform_architecture architecture() const noexcept;

        bool operator==(const toolchain_version& other) const noexcept;
        bool operator!=(const toolchain_version& other) const noexcept;

        bool is_compatible() const noexcept;

        static toolchain_version current();

    private:
        uint64_t value;
    };
}

#endif