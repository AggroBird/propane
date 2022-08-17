#ifndef _HEADER_PROPANE_ASSEMBLY
#define _HEADER_PROPANE_ASSEMBLY

#include "propane_runtime.hpp"

namespace propane
{
    class assembly
    {
    public:
        assembly() = default;
        explicit assembly(const class intermediate&, const class runtime&);
        explicit assembly(const class intermediate&);
        ~assembly() = default;

        assembly(const assembly&) = default;
        assembly& operator=(const assembly&) = default;

        assembly(assembly&&) noexcept = default;
        assembly& operator=(assembly&&) noexcept = default;

        bool is_valid() const noexcept;
        operator bool() const noexcept;

        toolchain_version version() const noexcept;
        bool is_compatible() const noexcept;

        // Direct reference to assembly data
        const assembly_data& assembly_ref() const noexcept;
        // Byte data of assembly (minus validation header/footer)
        std::span<const uint8_t> assembly_binary() const noexcept;
        // All data (including validation header/footer)
        std::span<const uint8_t> data() const noexcept;

        bool load(std::span<const uint8_t> from_bytes);

    private:
        friend class asm_assembly_data;
        block<uint8_t> content;
    };
}

#endif