#ifndef _HEADER_RUNTIME
#define _HEADER_RUNTIME

#include "propane_runtime.hpp"
#include "propane_intermediate.hpp"
#include "propane_assembly.hpp"

#include "serializable.hpp"
#include "database.hpp"
#include "host.hpp"
#include "library.hpp"

namespace propane
{
    // Opcodes
    enum class opcode : uint8_t
    {
        noop,

        set,
        conv,

        ari_not,
        ari_neg,
        ari_mul,
        ari_div,
        ari_mod,
        ari_add,
        ari_sub,
        ari_lsh,
        ari_rsh,
        ari_and,
        ari_xor,
        ari_or,

        padd,
        psub,
        pdif,

        cmp,
        ceq,
        cne,
        cgt,
        cge,
        clt,
        cle,
        cze,
        cnz,

        br,
        beq,
        bne,
        bgt,
        bge,
        blt,
        ble,
        bze,
        bnz,

        sw,

        call,
        callv,
        ret,
        retv,

        dump,
    };

    enum class subcode : uint8_t { invalid = 0xFF };

    constexpr opcode operator+(opcode lhs, opcode rhs) noexcept
    {
        return opcode(uint32_t(lhs) + uint32_t(rhs));
    }
    constexpr opcode operator-(opcode lhs, opcode rhs) noexcept
    {
        return opcode(uint32_t(lhs) - uint32_t(rhs));
    }

    // Global indices
    enum class global_flags : index_t
    {
        constant_flag = index_t(1) << (address_header::index_bit_count - 1),
        constant_mask = (address_header::index_max >> 1),
    };
    constexpr global_idx operator|(global_idx lhs, global_flags rhs) noexcept
    {
        return global_idx(index_t(lhs) | index_t(rhs));
    }
    constexpr global_idx& operator|=(global_idx& lhs, global_flags rhs) noexcept
    {
        lhs = lhs | rhs;
        return lhs;
    }
    constexpr global_idx operator&(global_idx lhs, global_flags rhs) noexcept
    {
        return global_idx(index_t(lhs) & index_t(rhs));
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

    // These are commonly used in switch tabes
    // because they are all (almost) equal length
    typedef int8_t i8;
    typedef uint8_t u8;
    typedef int16_t i16;
    typedef uint16_t u16;
    typedef int32_t i32;
    typedef uint32_t u32;
    typedef int64_t i64;
    typedef uint64_t u64;
    typedef float f32;
    typedef double f64;
    typedef void* vptr;

    // Bytecode pointers
    using pointer_t = uint8_t*;
    using const_pointer_t = const uint8_t*;

    inline pointer_t dereference(pointer_t addr) noexcept
    {
        return *reinterpret_cast<pointer_t*>(addr);
    }
    inline const_pointer_t dereference(const_pointer_t addr) noexcept
    {
        return *reinterpret_cast<const const_pointer_t*>(addr);
    }

    // Derivatives
    struct base_type_info
    {
        constexpr base_type_info(const char* name, type_idx type, size_t size) :
            name(name),
            type(type),
            size(size)
        {

        }

        template<typename value_t> static constexpr base_type_info make(const char* name)
        {
            return base_type_info(name, derive_type_index_v<value_t>, derive_base_size_v<value_t>);
        }

        string_view name;
        type_idx type;
        size_t size;
    };

    constexpr base_type_info base_types[] =
    {
        base_type_info::make<i8>("byte"),
        base_type_info::make<u8>("ubyte"),
        base_type_info::make<i16>("short"),
        base_type_info::make<u16>("ushort"),
        base_type_info::make<i32>("int"),
        base_type_info::make<u32>("uint"),
        base_type_info::make<i64>("long"),
        base_type_info::make<u64>("ulong"),
        base_type_info::make<f32>("float"),
        base_type_info::make<f64>("double"),
        base_type_info::make<vptr>("void*"),
        base_type_info::make<void>("void"),
    };
    constexpr size_t base_type_count() noexcept
    {
        return sizeof(base_types) / sizeof(base_type_info);
    }

    constexpr base_type_info alias_types[] =
    {
        base_type_info::make<offset_t>("offset"),
        base_type_info::make<size_t>("size"),
    };
    constexpr size_t alias_type_count() noexcept
    {
        return sizeof(alias_types) / sizeof(base_type_info);
    }

    constexpr bool is_base_type(type_idx key) noexcept
    {
        return size_t(key) < base_type_count();
    }

    constexpr size_t get_base_type_size(type_idx btype) noexcept
    {
        if (btype <= type_idx::vptr) return base_types[size_t(btype)].size;
        return 0;
    }


    // Lookups/translations
    enum class lookup_type : index_t
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

        lookup_idx(lookup_type lookup, index_t index) :
            lookup(lookup),
            index(index) {}

        lookup_idx(type_idx type) :
            lookup(lookup_type::type),
            type(type) {}

        lookup_idx(method_idx method) :
            lookup(lookup_type::method),
            method(method) {}

        inline static lookup_idx make_global(index_t index) noexcept
        {
            return lookup_idx(lookup_type::global, index);
        }
        inline static lookup_idx make_constant(index_t index) noexcept
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
            index_t index;
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

    // Address
    struct address_t
    {
        address_t(const type* type = nullptr, pointer_t addr = nullptr) :
            type_ptr(type),
            addr(addr) {}

        const type* type_ptr;
        pointer_t addr;
    };

    struct const_address_t
    {
        const_address_t(const type* type = nullptr, const_pointer_t addr = nullptr) :
            type_ptr(type),
            addr(addr) {}
        const_address_t(const address_t& addr) :
            type_ptr(addr.type_ptr),
            addr(addr.addr) {}

        const type* type_ptr;
        const_pointer_t addr;
    };


    struct address_data_t
    {
        address_data_t() = default;
        address_data_t(index_t init) :
            header(init),
            offset(0) {}

        address_header header;
        union
        {
            offset_idx field;
            aligned_offset_t offset;
        };
    };
    static_assert(sizeof(address_data_t) == sizeof(index_t) + sizeof(size_t), "Address size mismatch");

    // Read/write bytecode
    template<typename value_t> value_t read_bytecode(pointer_t& iptr) noexcept
    {
        static_assert(std::is_trivial_v<value_t>, "Trivial type required");
        return *reinterpret_cast<value_t*&>(iptr)++;
    }
    template<typename value_t> value_t& read_bytecode_ref(pointer_t& iptr) noexcept
    {
        static_assert(std::is_trivial_v<value_t>, "Trivial type required");
        return *reinterpret_cast<value_t*&>(iptr)++;
    }
    template<typename value_t> value_t read_bytecode(const_pointer_t& iptr) noexcept
    {
        static_assert(std::is_trivial_v<value_t>, "Trivial type required");
        return *reinterpret_cast<const value_t*&>(iptr)++;
    }
    template<typename value_t> void write_bytecode(pointer_t& iptr, const value_t& data) noexcept
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

    // Stack frame
    struct stack_frame_t
    {
        stack_frame_t() = default;
        stack_frame_t(const_pointer_t ibeg, const_pointer_t iend, const_pointer_t iptr, size_t return_offset, size_t frame_offset, size_t param_offset, size_t stack_offset, size_t stack_end, const method* minf) :
            ibeg(ibeg),
            iend(iend),
            iptr(iptr),
            return_offset(return_offset),
            frame_offset(frame_offset),
            param_offset(param_offset),
            stack_offset(stack_offset),
            stack_end(stack_end),
            minf(minf) {}

        const_pointer_t ibeg = nullptr;
        const_pointer_t iend = nullptr;
        const_pointer_t iptr = nullptr;
        size_t return_offset = 0;
        size_t frame_offset = 0;
        size_t param_offset = 0;
        size_t stack_offset = 0;
        size_t stack_end = 0;
        const method* minf = nullptr;
    };

    // Serialization of generic types
    template<typename value_t> struct propane::serialization::is_packed<aligned_t<value_t, alignof(index_t)>> { static constexpr bool value = true; };
    template<> struct propane::serialization::is_packed<translate_idx> { static constexpr bool value = true; };

    SERIALIZABLE(stackvar, type, offset);
    SERIALIZABLE(field, name, type, offset);
    SERIALIZABLE(generated_type, pointer);
    SERIALIZABLE(string_offset, offset, length);
    SERIALIZABLE(metadata, index, line_number);


    // Read/write
    template<typename src_t> src_t read(const_address_t addr) noexcept
    {
        static_assert(std::is_trivial<src_t>::value, "Type must be trivial");
        const src_t src = *reinterpret_cast<const src_t*>(addr.addr);
        return src;
    }
    template<typename src_t> src_t read(const void* ptr) noexcept
    {
        static_assert(std::is_trivial<src_t>::value, "Type must be trivial");
        const src_t src = *reinterpret_cast<const src_t*>(ptr);
        return src;
    }
    template<typename dst_t> dst_t& write(address_t addr) noexcept
    {
        static_assert(std::is_trivial_v<dst_t>, "Type must be trivial");
        dst_t& dst = *reinterpret_cast<dst_t*>(addr.addr);
        return dst;
    }
    template<typename dst_t> dst_t& write(void* ptr) noexcept
    {
        static_assert(std::is_trivial_v<dst_t>, "Type must be trivial");
        dst_t& dst = *reinterpret_cast<dst_t*>(ptr);
        return dst;
    }

    // Compare
    template<typename value_t> constexpr int32_t compare(value_t lhs, value_t rhs)
    {
        static_assert(std::is_arithmetic_v<value_t> || std::is_pointer_v<value_t>, "Arithmetic type required for compare");
        return (lhs < rhs) ? -1 : (lhs > rhs) ? 1 : 0;
    }

    // Runtime data
    class runtime_data
    {
    public:
        NOCOPY_CLASS_DEFAULT(runtime_data) = default;

        database<name_idx, host_library> libraries;
        indexed_vector<index_t, external_call_info> calls;
        unordered_map<string_view, index_t> call_lookup;

        size_t rehash() noexcept;

        void set_dirty();

    private:
        size_t hash_value = 0;
        bool modified = true;
    };
}

#endif