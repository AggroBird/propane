#ifndef _HEADER_UTILITY
#define _HEADER_UTILITY

#include "propane_common.hpp"
#include "runtime.hpp"

namespace propane
{
    constexpr string_view null_keyword = "null";

    // Parsing
    inline bool is_literal(char c) noexcept
    {
        return (c >= '0' && c <= '9') || c == '-';
    }
    inline bool is_literal(string_view str) noexcept
    {
        return !str.empty() && (is_literal(str[0]) || str == null_keyword);
    }
    inline bool is_address(char c) noexcept
    {
        return (c == '.' || c == '-' || c == '{' || c == '[' || c == '(' || c == '!' || c == '&');
    }
    inline bool is_address(string_view str) noexcept
    {
        return !str.empty() && is_address(str[0]);
    }
    inline bool is_identifier(char c, bool first = true) noexcept
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$')
        {
            return true;
        }
        if (!first)
        {
            return (c >= '0' && c <= '9');
        }
        return false;
    }
    inline bool is_identifier(string_view str) noexcept
    {
        if (!str.empty() && is_identifier(str[0]) && str != null_keyword)
        {
            for (size_t i = 1; i < str.size(); i++)
            {
                if (!is_identifier(str[i], false))
                {
                    return false;
                }
            }
            return true;
        }
        return false;
    }
    inline bool is_generated(string_view type_name) noexcept
    {
        return std::min(type_name.find('*'), type_name.find('[')) != string::npos;
    }


    string_view opcode_str(opcode op);
}

std::ostream& operator<<(std::ostream& stream, const propane::lookup_type& type);
std::ostream& operator<<(std::ostream& stream, const propane::file_meta& meta);
std::ostream& operator<<(std::ostream& stream, const propane::opcode& op);
std::ostream& operator<<(std::ostream& stream, const propane::toolchain_version& op);

template<typename index_type> inline string_view get_index_type_name(index_type) { return "Index"; }
inline string_view get_index_type_name(propane::type_idx) { return "Type index"; }
inline string_view get_index_type_name(propane::method_idx) { return "Method index"; }
inline string_view get_index_type_name(propane::name_idx) { return "Name index"; }
inline string_view get_index_type_name(propane::label_idx) { return "Label index"; }
inline string_view get_index_type_name(propane::offset_idx) { return "Offset index"; }
inline string_view get_index_type_name(propane::global_idx) { return "Global index"; }
inline string_view get_index_type_name(propane::meta_idx) { return "Meta index"; }

#endif