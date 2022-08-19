#include "propane_assembly.hpp"
#include "constants.hpp"
#include "name_generator.hpp"

namespace propane
{
    bool assembly::is_valid() const noexcept
    {
        return constants::validate_assembly_header(content);
    }
    assembly::operator bool() const noexcept
    {
        return is_valid();
    }

    toolchain_version assembly::version() const noexcept
    {
        if (content.size() >= constants::as_data_offset)
        {
            return *reinterpret_cast<const toolchain_version*>(content.data() + constants::assembly_header.size());
        }
        return toolchain_version();
    }
    bool assembly::is_compatible() const noexcept
    {
        return version().is_compatible();
    }

    const assembly_data& assembly::assembly_ref() const noexcept
    {
        if (is_valid())
        {
            return *reinterpret_cast<const assembly_data*>(content.data() + constants::as_data_offset);
        }

        static thread_local assembly_data empty_assembly;
        memset(&empty_assembly, 0, sizeof(empty_assembly));
        return empty_assembly;
    }
    span<const uint8_t> assembly::assembly_binary() const noexcept
    {
        if (is_valid())
        {
            return span<const uint8_t>(content.data() + constants::as_data_offset, content.size() - constants::as_total_size);
        }

        return span<const uint8_t>();
    }

    span<const uint8_t> assembly::data() const noexcept
    {
        return content;
    }
    bool assembly::load(span<const uint8_t> from_bytes)
    {
        if (!constants::validate_assembly_header(from_bytes)) return false;

        content = block<uint8_t>(from_bytes.data(), from_bytes.size());
        return true;
    }

    void assembly_data::generate_name(type_idx type, string& out_name) const
    {
        name_generator(type, out_name, types, signatures, database);
    }
}