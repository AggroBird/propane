#ifndef _HEADER_PROPANE_COMMON
#define _HEADER_PROPANE_COMMON

#include "propane_version.hpp"

#include <span>
#include <string>
#include <string_view>
#include <initializer_list>
#include <type_traits>
#include <exception>

namespace propane
{
    // Source/destination languages
    enum : uint32_t
    {
        language_propane,
        language_c,
    };

    // Index types
    using index_t = uint32_t;
    enum : index_t { invalid_index = 0xFFFFFFFF };

    enum class type_idx : index_t
    {
        i8,
        u8,
        i16,
        u16,
        i32,
        u32,
        i64,
        u64,
        f32,
        f64,
        vptr,
        voidtype,

        invalid = invalid_index,
    };

    constexpr bool is_integral(type_idx type) noexcept
    {
        return type < type_idx::f32;
    }
    constexpr bool is_unsigned(type_idx type) noexcept
    {
        return is_integral(type) && (((index_t)type & 1) == 1);
    }
    constexpr bool is_floating_point(type_idx type) noexcept
    {
        return type == type_idx::f32 || type == type_idx::f64;
    }
    constexpr bool is_arithmetic(type_idx type) noexcept
    {
        return type <= type_idx::f64;
    }

    enum class method_idx : index_t { invalid = invalid_index };
    enum class signature_idx : index_t { invalid = invalid_index };
    enum class name_idx : index_t { invalid = invalid_index };
    enum class label_idx : index_t { invalid = invalid_index };
    enum class offset_idx : index_t { invalid = invalid_index };
    enum class global_idx : index_t { invalid = invalid_index };
    enum class meta_idx : index_t { invalid = invalid_index };


    // Typedefs
    static_assert(std::is_unsigned_v<size_t>, "size_t is not unsigned");
    using offset_t = std::make_signed_t<size_t>;


    // Aligned type
    template<typename value_t, size_t alignment> struct aligned_t
    {
        using type = value_t;

        aligned_t() noexcept = default;
        aligned_t(const aligned_t&) noexcept = default;
        aligned_t& operator=(const aligned_t&) noexcept = default;

        inline aligned_t(value_t val) noexcept
        {
            reinterpret_cast<value_t&>(data) = val;
        }
        inline aligned_t& operator=(value_t val) noexcept
        {
            reinterpret_cast<value_t&>(data) = val;
            return *this;
        }
        inline value_t& operator*() noexcept
        {
            return reinterpret_cast<value_t&>(data);
        }
        inline operator value_t() const noexcept
        {
            return reinterpret_cast<const value_t&>(data);
        }

        std::aligned_storage_t<sizeof(value_t), alignment> data;
    };
    using aligned_size_t = aligned_t<size_t, alignof(index_t)>;
    using aligned_offset_t = aligned_t<offset_t, alignof(index_t)>;


    // Addresses
    enum class address_type : uint8_t
    {
        stackvar = 0,
        parameter,
        global,
        constant,
    };

    enum class address_prefix : uint8_t
    {
        none = 0,
        indirection,
        address_of,
        size_of,
    };

    enum class address_modifier : uint8_t
    {
        none = 0,
        direct_field,
        indirect_field,
        subscript,
    };

    namespace address_header_constants
    {
        static constexpr index_t flag_mask = 0b11;
        static constexpr index_t index_bit_count = 26;
        static constexpr index_t type_offset = 30;
        static constexpr index_t prefix_offset = 28;
        static constexpr index_t modifier_offset = 26;
        static constexpr index_t index_max = ~index_t(0) >> (32 - address_header_constants::index_bit_count);
    }

    struct address_header
    {
        address_header() noexcept = default;
        address_header(index_t init) noexcept :
            value(init) {}
        address_header(address_type type, address_prefix prefix, address_modifier modifier, index_t index) noexcept
        {
            value = (index_t(index) & address_header_constants::index_max);
            value |= ((index_t(type) & address_header_constants::flag_mask) << address_header_constants::type_offset);
            value |= ((index_t(prefix) & address_header_constants::flag_mask) << address_header_constants::prefix_offset);
            value |= ((index_t(modifier) & address_header_constants::flag_mask) << address_header_constants::modifier_offset);
        }
        address_header(type_idx constant_type) noexcept
        {
            value = index_t(constant_type) & address_header_constants::index_max;
            value |= ((index_t(address_type::constant) & address_header_constants::flag_mask) << address_header_constants::type_offset);
        }

        inline const address_type type() const noexcept
        {
            return address_type((value >> address_header_constants::type_offset) & address_header_constants::flag_mask);
        }
        inline const address_prefix prefix() const noexcept
        {
            return address_prefix((value >> address_header_constants::prefix_offset) & address_header_constants::flag_mask);
        }
        inline const address_modifier modifier() const noexcept
        {
            return address_modifier((value >> address_header_constants::modifier_offset) & address_header_constants::flag_mask);
        }
        inline index_t index() const noexcept
        {
            return index_t(value & address_header_constants::index_max);
        }

        inline void set_type(address_type type) noexcept
        {
            value &= ~(address_header_constants::flag_mask << address_header_constants::type_offset);
            value |= ((index_t(type) & address_header_constants::flag_mask) << address_header_constants::type_offset);
        }
        inline void set_prefix(address_prefix prefix) noexcept
        {
            value &= ~(address_header_constants::flag_mask << address_header_constants::prefix_offset);
            value |= ((index_t(prefix) & address_header_constants::flag_mask) << address_header_constants::prefix_offset);
        }
        inline void set_modifier(address_modifier modifier) noexcept
        {
            value &= ~(address_header_constants::flag_mask << address_header_constants::modifier_offset);
            value |= ((index_t(modifier) & address_header_constants::flag_mask) << address_header_constants::modifier_offset);
        }
        inline void set_index(index_t index) noexcept
        {
            value &= ~address_header_constants::index_max;
            value |= (index_t(index) & address_header_constants::index_max);
        }

        inline bool operator==(const address_header& other) const noexcept
        {
            return value == other.value;
        }
        inline bool operator!=(const address_header& other) const noexcept
        {
            return value != other.value;
        }

    private:
        index_t value;
    };

    // Type flags
    enum class type_flags : index_t
    {
        none = 0,
        is_union = 1 << 0,
        is_external = 1 << 1,

        is_pointer_type = 1 << 8,
        is_array_type = 1 << 9,
        is_signature_type = 1 << 10,

        is_generated_type = (is_pointer_type | is_array_type | is_signature_type),
    };

    constexpr type_flags operator|(type_flags lhs, type_flags rhs) noexcept
    {
        return type_flags(index_t(lhs) | index_t(rhs));
    }
    constexpr type_flags& operator|=(type_flags& lhs, type_flags rhs) noexcept
    {
        lhs = lhs | rhs;
        return lhs;
    }
    constexpr bool operator&(type_flags lhs, type_flags rhs) noexcept
    {
        return type_flags(index_t(lhs) & index_t(rhs)) != type_flags::none;
    }


    struct file_meta
    {
        file_meta() = default;
        file_meta(std::string_view file_name, uint32_t line_number) :
            file_name(file_name),
            line_number(line_number) {}

        std::string_view file_name;
        uint32_t line_number = 0;
    };

    struct string_offset
    {
        string_offset() = default;
        string_offset(index_t offset, index_t length) :
            offset(offset),
            length(length) {}

        index_t offset;
        index_t length;
    };

    template<typename value_t, size_t size> class handle
    {
    protected:
        template<typename... args> handle(args&... arg)
        {
            static_assert(sizeof(value_t) <= size, "Value type size greater than buffer size");
            *new (reinterpret_cast<value_t*>(data.bytes)) value_t(arg...);
        }
        ~handle()
        {
            reinterpret_cast<value_t*>(data.bytes)->~value_t();
            memset(data.bytes, 0, size);
        }

        handle(const handle&) = delete;
        handle& operator=(const handle&) = delete;

        inline value_t& self() noexcept
        {
            return reinterpret_cast<value_t&>(data);
        }
        inline const value_t& self() const noexcept
        {
            return reinterpret_cast<const value_t&>(data);
        }

    private:
        struct
        {
            uint8_t bytes[size];
        } data;
    };

    // Span utility
    template<typename value_t> inline std::span<const value_t> init_span(std::initializer_list<value_t> init) noexcept
    {
        return std::span<const value_t>(init.begin(), init.size());
    }
    template<typename value_t> inline std::span<const value_t> init_span(const value_t& init) noexcept
    {
        return std::span<const value_t>(&init, 1);
    }

    // Exceptions
    class propane_exception : public std::exception
    {
    public:
        propane_exception(uint32_t errc, const char* const msg) :
            errc(errc),
            msg(msg) {}

        inline uint32_t error_code() const noexcept
        {
            return errc;
        }

        virtual const char* what() const noexcept override
        {
            return msg.data();
        }

    private:
        uint32_t errc;
        std::string msg;
    };

    class generator_exception : public propane_exception
    {
    public:
        generator_exception(uint32_t errc, const char* const msg) :
            propane_exception(errc, msg),
            line(0) {}
        generator_exception(uint32_t errc, const char* const msg, std::string_view file, uint32_t line) :
            propane_exception(errc, msg),
            file(file),
            line(line) {}

        inline std::string_view file_name() const noexcept
        {
            return file;
        }
        inline uint32_t line_number() const noexcept
        {
            return line;
        }

    private:
        std::string file;
        uint32_t line;
    };

    class merger_exception : public propane_exception
    {
    public:
        using propane_exception::propane_exception;
    };

    class linker_exception : public propane_exception
    {
    public:
        using propane_exception::propane_exception;
    };

    class runtime_exception : public propane_exception
    {
    public:
        using propane_exception::propane_exception;
    };
}

#endif