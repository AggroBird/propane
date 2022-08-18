#ifndef _HEADER_RUNTIME
#define _HEADER_RUNTIME

#include "propane_runtime.hpp"
#include "propane_intermediate.hpp"
#include "propane_assembly.hpp"

#include "opcodes.hpp"
#include "serializable.hpp"
#include "database.hpp"
#include "host.hpp"
#include "library.hpp"

namespace propane
{
    // Global indices
    enum class global_flags : uint32_t
    {
        constant_flag = uint32_t(1) << (address_header_constants::index_bit_count - 1),
        constant_mask = (address_header_constants::index_max >> 1),
    };
    constexpr global_idx operator|(global_idx lhs, global_flags rhs) noexcept
    {
        return global_idx(uint32_t(lhs) | uint32_t(rhs));
    }
    constexpr global_idx& operator|=(global_idx& lhs, global_flags rhs) noexcept
    {
        lhs = lhs | rhs;
        return lhs;
    }
    constexpr global_idx operator&(global_idx lhs, global_flags rhs) noexcept
    {
        return global_idx(uint32_t(lhs) & uint32_t(rhs));
    }
    constexpr global_idx& operator&=(global_idx& lhs, global_flags rhs) noexcept
    {
        lhs = lhs & rhs;
        return lhs;
    }
    constexpr bool is_constant_flag_set(global_idx idx) noexcept
    {
        return (idx & global_flags::constant_flag) != global_idx(0);
    }

    // Pointer ops
    inline uint8_t* dereference(uint8_t* addr) noexcept
    {
        return *reinterpret_cast<uint8_t**>(addr);
    }
    inline const uint8_t* dereference(const uint8_t* addr) noexcept
    {
        return *reinterpret_cast<const uint8_t* const*>(addr);
    }

    constexpr native_type_info_t base_types[] =
    {
        native_type_info_v<i8>,
        native_type_info_v<u8>,
        native_type_info_v<i16>,
        native_type_info_v<u16>,
        native_type_info_v<i32>,
        native_type_info_v<u32>,
        native_type_info_v<i64>,
        native_type_info_v<u64>,
        native_type_info_v<f32>,
        native_type_info_v<f64>,
        native_type_info_v<vptr>,
        native_type_info_v<void>,
    };
    constexpr size_t base_type_count = sizeof(base_types) / sizeof(native_type_info_t);

    constexpr native_type_info_t alias_types[] =
    {
        native_type_info_t(native_alias_name_v<size_t>, derive_type_index_v<size_t>, native_type_size_v<size_t>),
        native_type_info_t(native_alias_name_v<offset_t>, derive_type_index_v<offset_t>, native_type_size_v<offset_t>),
    };
    constexpr size_t alias_type_count = sizeof(alias_types) / sizeof(native_type_info_t);

    constexpr bool is_base_type(type_idx key) noexcept
    {
        return static_cast<size_t>(key) < base_type_count;
    }

    constexpr size_t get_base_type_size(type_idx btype) noexcept
    {
        if (btype <= type_idx::vptr) return base_types[static_cast<size_t>(btype)].size;
        return 0;
    }


    // Lookups/translations
    enum class lookup_type : uint32_t
    {
        type,
        method,
        global,
        constant,
        identifier,
    };

    struct lookup_idx
    {
        lookup_idx() = default;

        lookup_idx(lookup_type lookup, uint32_t index) :
            lookup(lookup),
            index(index) {}

        lookup_idx(type_idx type) :
            lookup(lookup_type::type),
            type(type) {}

        lookup_idx(method_idx method) :
            lookup(lookup_type::method),
            method(method) {}

        inline static lookup_idx make_global(uint32_t index) noexcept
        {
            return lookup_idx(lookup_type::global, index);
        }
        inline static lookup_idx make_constant(uint32_t index) noexcept
        {
            return lookup_idx(lookup_type::constant, index);
        }
        inline static lookup_idx make_identifier() noexcept
        {
            return lookup_idx(lookup_type::identifier, invalid_index);
        }

        lookup_type lookup;

        union
        {
            type_idx type;
            method_idx method;
            uint32_t index;
        };

        inline bool operator==(type_idx idx) const noexcept
        {
            if (lookup == lookup_type::type)
            {
                return type == idx;
            }
            return false;
        }
        inline bool operator!=(type_idx idx) const noexcept
        {
            return !(*this == idx);
        }
        inline bool operator==(method_idx idx) const noexcept
        {
            if (lookup == lookup_type::method)
            {
                return method == idx;
            }
            return false;
        }
        inline bool operator!=(method_idx idx) const noexcept
        {
            return !(*this == idx);
        }
    };

    union translate_idx
    {
        translate_idx() = default;
        translate_idx(name_idx name) : name(name) {}

        name_idx name;
        global_idx index;
    };

    struct address_data_t
    {
        address_data_t() = default;
        address_data_t(uint32_t init) :
            header(init),
            offset(0) {}

        address_header header;
        union
        {
            offset_idx field;
            aligned_offset_t offset;
        };
    };
    static_assert(sizeof(address_data_t) == sizeof(uint32_t) + sizeof(size_t), "Address size mismatch");

    // Read/write bytecode
    template<typename value_t> value_t read_bytecode(uint8_t*& iptr) noexcept
    {
        static_assert(std::is_trivial_v<value_t>, "Trivial type required");
        return *reinterpret_cast<value_t*&>(iptr)++;
    }
    template<typename value_t> value_t& read_bytecode_ref(uint8_t*& iptr) noexcept
    {
        static_assert(std::is_trivial_v<value_t>, "Trivial type required");
        return *reinterpret_cast<value_t*&>(iptr)++;
    }
    template<typename value_t> value_t read_bytecode(const uint8_t*& iptr) noexcept
    {
        static_assert(std::is_trivial_v<value_t>, "Trivial type required");
        return *reinterpret_cast<const value_t*&>(iptr)++;
    }
    template<typename value_t> void write_bytecode(uint8_t*& iptr, const value_t& data) noexcept
    {
        static_assert(std::is_trivial_v<value_t>, "Trivial type required");
        *reinterpret_cast<value_t*&>(iptr)++ = data;
    }
    template<typename value_t> void append_bytecode(vector<uint8_t>& buf, const value_t& data)
    {
        static_assert(std::is_trivial_v<value_t>, "Trivial type required");
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&data);
        buf.insert(buf.end(), ptr, ptr + sizeof(value_t));
    }
    inline void append_bytecode(vector<uint8_t>& buf, string_view str)
    {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(str.data());
        buf.insert(buf.end(), ptr, ptr + str.size());
    }

    // Serialization of generic types
    template<typename value_t> struct propane::serialization::is_packed<aligned_t<value_t, alignof(uint32_t)>> { static constexpr bool value = true; };
    template<> struct propane::serialization::is_packed<translate_idx> { static constexpr bool value = true; };

    SERIALIZABLE(stackvar, type, offset);
    SERIALIZABLE(field, name, type, offset);
    SERIALIZABLE(generated_type, pointer);
    SERIALIZABLE(string_offset, offset, length);
    SERIALIZABLE(metadata, index, line_number);

    // Compare
    template<typename value_t> constexpr int32_t compare(value_t lhs, value_t rhs)
    {
        static_assert(std::is_arithmetic_v<value_t> || std::is_pointer_v<value_t>, "Arithmetic type required for compare");
        return (lhs < rhs) ? -1 : (lhs > rhs) ? 1 : 0;
    }

    class library_info
    {
    public:
        library_info(string_view name, bool preload_symbols, const block<external_call_info>& calls) :
            name(name),
            preload_symbols(preload_symbols),
            calls(calls) {}

        string name;
        bool preload_symbols;
        indexed_block<uint32_t, external_call_info> calls;
    };

    struct runtime_call_index
    {
        runtime_call_index() = default;
        runtime_call_index(name_idx library, uint32_t index) :
            library(library),
            index(index) {}

        name_idx library;
        uint32_t index;
    };

    class environment_data
    {
    public:
        NOCOPY_CLASS_DEFAULT(environment_data) = default;

        map<string, const library_data*> libraries;
    };
    constexpr size_t environment_data_handle_size = approximate_handle_size(sizeof(environment_data));

    // Runtime data
    class runtime_data
    {
    public:
        NOCOPY_CLASS_DEFAULT(runtime_data) = default;

        indexed_vector<name_idx, library_info> libraries;
        unordered_map<string_view, runtime_call_index> call_lookup;
        unordered_map<string_view, native_type_info> type_lookup;
        size_t hash = 0;
    };
    constexpr size_t runtime_data_handle_size = approximate_handle_size(sizeof(runtime_data));
}

#endif