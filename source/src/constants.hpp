#ifndef _HEADER_CONSTANTS
#define _HEADER_CONSTANTS

#include "propane_common.hpp"
#include "common.hpp"

namespace propane
{
    namespace constants
    {
        constexpr string_view intermediate_header = "PINT";
        constexpr string_view assembly_header = "PASM";
        constexpr string_view footer = "END";

        bool validate_intermediate_header(span<const uint8_t> data) noexcept;
        bool validate_assembly_header(span<const uint8_t> data) noexcept;
    }
}

#endif