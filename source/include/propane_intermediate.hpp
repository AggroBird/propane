#ifndef _HEADER_PROPANE_INTERMEDIATE
#define _HEADER_PROPANE_INTERMEDIATE

#include "propane_common.hpp"
#include "propane_block.hpp"

namespace propane
{
    class intermediate
    {
    public:
        intermediate() = default;
        ~intermediate() = default;

        intermediate(const intermediate&) = default;
        intermediate& operator=(const intermediate&) = default;

        intermediate(intermediate&&) noexcept = default;
        intermediate& operator=(intermediate&&) noexcept = default;

        bool is_valid() const noexcept;
        operator bool() const noexcept;

        toolchain_version version() const noexcept;
        bool is_compatible() const noexcept;

        span<const uint8_t> data() const noexcept;
        bool load(span<const uint8_t> from_bytes);

        intermediate operator+(const intermediate&) const;
        intermediate& operator+=(const intermediate&);

    private:
        friend class gen_intermediate_data;
        block<uint8_t> content;
    };
}

#endif