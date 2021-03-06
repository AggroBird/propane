#include "propane_version.hpp"
#include "constants.hpp"
#include "version.hpp"

// Endian independant version numbering
// See https://sourceforge.net/p/predef/wiki/Endianness/

namespace propane
{
    namespace version
    {
        constexpr uint16_t major = PROPANE_VERSION_MAJOR;
        constexpr uint16_t minor = PROPANE_VERSION_MINOR;
        constexpr uint32_t changelist = PROPANE_VERSION_CHANGELIST;
    }

    constexpr uint64_t major_bytecount = 2;
    constexpr uint64_t minor_bytecount = 2;
    constexpr uint64_t changelist_bytecount = 3;
    constexpr uint64_t endian_arch_bytecount = 1;
    static_assert((major_bytecount + minor_bytecount + changelist_bytecount + endian_arch_bytecount) == sizeof(uint64_t));

    constexpr uint64_t major_offset = 0;
    constexpr uint64_t minor_offset = major_offset + major_bytecount;
    constexpr uint64_t changelist_offset = minor_offset + minor_bytecount;
    constexpr uint64_t endian_arch_offset = changelist_offset + changelist_bytecount;
    static_assert((endian_arch_offset + endian_arch_bytecount) == sizeof(uint64_t));


    union version_bytes
    {
        uint8_t bytes[8];
        uint64_t value;
    };

    template<uint64_t offset, uint64_t bytecount, typename value_t> inline void fold(uint64_t& value, value_t fold) noexcept
    {
        version_bytes& vb = reinterpret_cast<version_bytes&>(value);
        uint64_t i64 = uint64_t(fold);
        for (uint64_t i = 0; i < bytecount; i++)
        {
            vb.bytes[i + offset] = uint8_t(i64 & uint64_t(0xFF));
            i64 >>= 8;
        }
    }
    template<uint64_t offset, uint64_t bytecount, typename value_t> inline value_t unfold(uint64_t value) noexcept
    {
        const version_bytes& vb = reinterpret_cast<const version_bytes&>(value);
        uint64_t i64 = 0;
        for (uint64_t i = 0; i < bytecount; i++)
        {
            i64 <<= 8;
            i64 |= uint64_t(vb.bytes[((bytecount - 1) - i) + offset]);
        }
        return value_t(i64);
    }


    toolchain_version::toolchain_version() : value(0)
    {

    }
    toolchain_version::toolchain_version(uint16_t major, uint16_t minor, uint32_t changelist, platform_endianness endianness, platform_architecture architecture) : value(0)
    {
        fold<major_offset, major_bytecount>(value, major);
        fold<minor_offset, minor_bytecount>(value, minor);
        fold<changelist_offset, changelist_bytecount>(value, changelist);

        uint8_t endian_arch = 0;
        endian_arch |= uint8_t((int32_t(endianness) & 0xF) << 4);
        endian_arch |= uint8_t(int32_t(architecture) & 0xF);
        reinterpret_cast<version_bytes&>(value).bytes[endian_arch_offset] = endian_arch;
    }

    uint16_t toolchain_version::major() const noexcept
    {
        return unfold<major_offset, major_bytecount, uint16_t>(value);
    }
    uint16_t toolchain_version::minor() const noexcept
    {
        return unfold<minor_offset, minor_bytecount, uint16_t>(value);
    }
    uint32_t toolchain_version::changelist() const noexcept
    {
        return unfold<changelist_offset, changelist_bytecount, uint32_t>(value);
    }

    platform_endianness toolchain_version::endianness() const noexcept
    {
        return platform_endianness((reinterpret_cast<const version_bytes&>(value).bytes[endian_arch_offset] >> 4) & 0xF);
    }
    platform_architecture toolchain_version::architecture() const noexcept
    {
        return platform_architecture(reinterpret_cast<const version_bytes&>(value).bytes[endian_arch_offset] & 0xF);
    }

    bool toolchain_version::operator==(const toolchain_version& other) const noexcept
    {
        return value == other.value;
    }
    bool toolchain_version::operator!=(const toolchain_version& other) const noexcept
    {
        return value != other.value;
    }


    inline platform_endianness get_endianness() noexcept
    {
        union u32_bytes
        {
            uint32_t value;
            uint8_t bytes[sizeof(uint32_t)];
        } u32;

        u32.bytes[0] = 0x01;
        u32.bytes[1] = 0x02;
        u32.bytes[2] = 0x03;
        u32.bytes[3] = 0x04;

        switch (u32.value)
        {
            case uint32_t(0x04030201): return platform_endianness::little;
            case uint32_t(0x01020304): return platform_endianness::big;
            case uint32_t(0x02010403): return platform_endianness::little_word;
            case uint32_t(0x03040102): return platform_endianness::big_word;
        }
        return platform_endianness::unknown;
    }
    inline platform_architecture get_architecture() noexcept
    {
        switch (sizeof(void*))
        {
            case 4: return platform_architecture::x32;
            case 8: return platform_architecture::x64;
        }
        return platform_architecture::unknown;
    }

    bool toolchain_version::is_compatible() const noexcept
    {
        return
            major() == version::major &&
            minor() == version::minor &&
            // By default a different changelist does not invalidate previous binaries.
            // Uncomment this to include changelist into the compatibility check:
            //changelist() == version::changelist &&
            endianness() == get_endianness() &&
            architecture() == get_architecture();
    }

    toolchain_version toolchain_version::current()
    {
        return toolchain_version(version::major, version::minor, version::changelist, get_endianness(), get_architecture());
    }


    inline bool compare_header(span<const uint8_t> data, string_view header)
    {
        const string_view str((const char*)data.data(), header.size());
        return header == str;
    }
    inline bool compare_footer(span<const uint8_t> data)
    {
        const string_view str((const char*)data.data() + (data.size() - constants::footer.size()), constants::footer.size());
        return constants::footer == str;
    }
    bool constants::validate_intermediate_header(span<const uint8_t> data) noexcept
    {
        if (data.size() >= constants::intermediate_header.size() + constants::footer.size())
        {
            return
                compare_header(data, constants::intermediate_header) &&
                compare_footer(data);
        }
        return false;
    }
    bool constants::validate_assembly_header(span<const uint8_t> data) noexcept
    {
        if (data.size() >= constants::assembly_header.size() + constants::footer.size())
        {
            return
                compare_header(data, constants::assembly_header) &&
                compare_footer(data);
        }
        return false;
    }
}