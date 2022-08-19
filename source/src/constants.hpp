#ifndef _HEADER_CONSTANTS
#define _HEADER_CONSTANTS

#include "propane_common.hpp"
#include "common.hpp"

namespace propane
{
    namespace constants
    {
        inline constexpr string_view intermediate_header = "PINT";
        inline constexpr string_view assembly_header = "PASM";
        inline constexpr string_view footer = "END";

        constexpr size_t im_data_offset = intermediate_header.size() + sizeof(toolchain_version);
        constexpr size_t as_data_offset = assembly_header.size() + sizeof(toolchain_version);
        constexpr size_t as_total_size = as_data_offset + footer.size();

        bool validate_intermediate_header(span<const uint8_t> data) noexcept;
        bool validate_assembly_header(span<const uint8_t> data) noexcept;
    }
}

#endif