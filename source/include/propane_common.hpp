#ifndef _HEADER_PROPANE_COMMON
#define _HEADER_PROPANE_COMMON

#include "propane_version.hpp"
#include "propane_span.hpp"

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

    inline constexpr std::string_view null_keyword = "null";

    // Typedefs
    static_assert(std::is_unsigned_v<size_t>, "size_t is not unsigned");
    using offset_t = std::make_signed_t<size_t>;
    typedef void(*method_handle)();

    // Index types
    enum : uint32_t { invalid_index = 0xFFFFFFFF };

    enum class type_idx : uint32_t
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

    // Ensure void size is 0
    template<typename value_t> constexpr size_t native_type_size_v = sizeof(value_t);
    template<> constexpr size_t native_type_size_v<void> = 0;

    // Derive pointer depth
    template<typename value_t> struct derive_pointer_info
    {
        typedef value_t base_type;
        static constexpr size_t depth = 0;
    };
    template<typename value_t> struct derive_pointer_info<value_t*> : public derive_pointer_info<value_t>
    {
        static constexpr size_t depth = derive_pointer_info<value_t>::depth + 1;
    };
    template<typename value_t> constexpr size_t derive_pointer_depth_v = derive_pointer_info<value_t>::depth;

    struct native_field_info
    {
        constexpr native_field_info() = default;
        constexpr native_field_info(std::string_view name, size_t offset, std::string_view type) :
            name(name),
            offset(offset),
            type(type) {}

        std::string_view name;
        size_t offset = 0;
        std::string_view type;
    };

    struct native_type_info
    {
        constexpr native_type_info() = default;
        constexpr native_type_info(std::string_view name, size_t size) :
            name(name),
            size(size),
            fields() {}
        constexpr native_type_info(std::string_view name, size_t size, span<const native_field_info> fields) :
            name(name),
            size(size),
            fields(fields) {}

        std::string_view name;
        size_t size = 0;
        span<const native_field_info> fields;
    };

    // Make type helper function
    template<typename value_t> static inline constexpr native_type_info make_type(std::string_view name)
    {
        return native_type_info(name, native_type_size_v<value_t>, span<const native_field_info>());
    }
    template<typename value_t> static inline constexpr native_type_info make_type(std::string_view name, span<const native_field_info> fields)
    {
        return native_type_info(name, native_type_size_v<value_t>, fields);
    }

    // Specializing this template allows for binding native structs to the runtime,
    // so they can be used as parameters in native library calls.
    // Since the runtime has no notion of padding, extra caution needs to be taken to ensure
    // the structs are properly packed have the same layout in both runtime and native environments.
    template<typename value_t> inline constexpr native_type_info native_type_info_v = make_type<value_t>(std::string_view());
    template<> inline constexpr native_type_info native_type_info_v<int8_t> = make_type<int8_t>("byte");
    template<> inline constexpr native_type_info native_type_info_v<uint8_t> = make_type<uint8_t>("ubyte");
    template<> inline constexpr native_type_info native_type_info_v<int16_t> = make_type<int16_t>("short");
    template<> inline constexpr native_type_info native_type_info_v<uint16_t> = make_type<uint16_t>("ushort");
    template<> inline constexpr native_type_info native_type_info_v<int32_t> = make_type<int32_t>("int");
    template<> inline constexpr native_type_info native_type_info_v<uint32_t> = make_type<uint32_t>("uint");
    template<> inline constexpr native_type_info native_type_info_v<int64_t> = make_type<int64_t>("long");
    template<> inline constexpr native_type_info native_type_info_v<uint64_t> = make_type<uint64_t>("ulong");
    template<> inline constexpr native_type_info native_type_info_v<float> = make_type<float>("float");
    template<> inline constexpr native_type_info native_type_info_v<double> = make_type<double>("double");
    template<> inline constexpr native_type_info native_type_info_v<void> = make_type<void>("void");

    // Alias types (for generator)
    template<typename value_t> inline constexpr std::string_view native_alias_name_v = std::string_view();
    template<> inline constexpr std::string_view native_alias_name_v<offset_t> = "offset";
    template<> inline constexpr std::string_view native_alias_name_v<size_t> = "size";

    // Make field helper function
    template<typename value_t> static inline constexpr native_field_info make_field(std::string_view name, size_t offset)
    {
        return native_field_info(name, offset, native_type_info_v<value_t>.name);
    }


    // Internal base type info
    struct base_type_info
    {
        constexpr base_type_info() :
            name(),
            index(type_idx::invalid),
            size(0) {}
        constexpr base_type_info(std::string_view name, type_idx index, size_t size) :
            name(name),
            index(index),
            size(size) {}

        std::string_view name;
        type_idx index;
        size_t size;

        template<typename value_t> static constexpr base_type_info make(type_idx type)
        {
            constexpr native_type_info type_info = native_type_info_v<value_t>;
            return base_type_info(type_info.name, type, type_info.size);
        }
    };

    template<typename value_t> inline constexpr base_type_info base_type_info_v = base_type_info();
    template<> inline constexpr base_type_info base_type_info_v<int8_t> = base_type_info::make<int8_t>(type_idx::i8);
    template<> inline constexpr base_type_info base_type_info_v<uint8_t> = base_type_info::make<uint8_t>(type_idx::u8);
    template<> inline constexpr base_type_info base_type_info_v<int16_t> = base_type_info::make<int16_t>(type_idx::i16);
    template<> inline constexpr base_type_info base_type_info_v<uint16_t> = base_type_info::make<uint16_t>(type_idx::u16);
    template<> inline constexpr base_type_info base_type_info_v<int32_t> = base_type_info::make<int32_t>(type_idx::i32);
    template<> inline constexpr base_type_info base_type_info_v<uint32_t> = base_type_info::make<uint32_t>(type_idx::u32);
    template<> inline constexpr base_type_info base_type_info_v<int64_t> = base_type_info::make<int64_t>(type_idx::i64);
    template<> inline constexpr base_type_info base_type_info_v<uint64_t> = base_type_info::make<uint64_t>(type_idx::u64);
    template<> inline constexpr base_type_info base_type_info_v<float> = base_type_info::make<float>(type_idx::f32);
    template<> inline constexpr base_type_info base_type_info_v<double> = base_type_info::make<double>(type_idx::f64);
    template<> inline constexpr base_type_info base_type_info_v<void*> = base_type_info::make<void*>(type_idx::vptr);
    template<> inline constexpr base_type_info base_type_info_v<void> = base_type_info::make<void>(type_idx::voidtype);

    template<typename value_t> inline constexpr type_idx derive_type_index_v = base_type_info_v<value_t>.index;

    inline constexpr bool is_integral(type_idx type) noexcept
    {
        return type < type_idx::f32;
    }
    inline constexpr bool is_unsigned(type_idx type) noexcept
    {
        return is_integral(type) && (((uint32_t)type & 1) == 1);
    }
    inline constexpr bool is_floating_point(type_idx type) noexcept
    {
        return type == type_idx::f32 || type == type_idx::f64;
    }
    inline constexpr bool is_arithmetic(type_idx type) noexcept
    {
        return type <= type_idx::f64;
    }

    enum class method_idx : uint32_t { invalid = invalid_index };
    enum class signature_idx : uint32_t { invalid = invalid_index };
    enum class name_idx : uint32_t { invalid = invalid_index };
    enum class label_idx : uint32_t { invalid = invalid_index };
    enum class offset_idx : uint32_t { invalid = invalid_index };
    enum class global_idx : uint32_t { invalid = invalid_index };
    enum class meta_idx : uint32_t { invalid = invalid_index };


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
    using aligned_size_t = aligned_t<size_t, alignof(uint32_t)>;
    using aligned_offset_t = aligned_t<offset_t, alignof(uint32_t)>;


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
        offset,
    };

    namespace address_header_constants
    {
        static constexpr uint32_t flag_mask = 0b11;
        static constexpr uint32_t index_bit_count = 26;
        static constexpr uint32_t type_offset = 30;
        static constexpr uint32_t prefix_offset = 28;
        static constexpr uint32_t modifier_offset = 26;
        static constexpr uint32_t index_max = ~0u >> (32 - address_header_constants::index_bit_count);
    }

    struct address_header
    {
        address_header() noexcept = default;
        address_header(uint32_t init) noexcept :
            value(init) {}
        address_header(address_type type, address_prefix prefix, address_modifier modifier, uint32_t index) noexcept
        {
            value = (static_cast<uint32_t>(index) & address_header_constants::index_max);
            value |= ((static_cast<uint32_t>(type) & address_header_constants::flag_mask) << address_header_constants::type_offset);
            value |= ((static_cast<uint32_t>(prefix) & address_header_constants::flag_mask) << address_header_constants::prefix_offset);
            value |= ((static_cast<uint32_t>(modifier) & address_header_constants::flag_mask) << address_header_constants::modifier_offset);
        }
        address_header(type_idx constant_type) noexcept
        {
            value = static_cast<uint32_t>(constant_type) & address_header_constants::index_max;
            value |= ((static_cast<uint32_t>(address_type::constant) & address_header_constants::flag_mask) << address_header_constants::type_offset);
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
        inline uint32_t index() const noexcept
        {
            return static_cast<uint32_t>(value & address_header_constants::index_max);
        }

        inline void set_type(address_type type) noexcept
        {
            value &= ~(address_header_constants::flag_mask << address_header_constants::type_offset);
            value |= ((static_cast<uint32_t>(type) & address_header_constants::flag_mask) << address_header_constants::type_offset);
        }
        inline void set_prefix(address_prefix prefix) noexcept
        {
            value &= ~(address_header_constants::flag_mask << address_header_constants::prefix_offset);
            value |= ((static_cast<uint32_t>(prefix) & address_header_constants::flag_mask) << address_header_constants::prefix_offset);
        }
        inline void set_modifier(address_modifier modifier) noexcept
        {
            value &= ~(address_header_constants::flag_mask << address_header_constants::modifier_offset);
            value |= ((static_cast<uint32_t>(modifier) & address_header_constants::flag_mask) << address_header_constants::modifier_offset);
        }
        inline void set_index(uint32_t index) noexcept
        {
            value &= ~address_header_constants::index_max;
            value |= (static_cast<uint32_t>(index) & address_header_constants::index_max);
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
        uint32_t value;
    };

    // Type flags
    enum class type_flags : uint32_t
    {
        none = 0,
        is_union = 1 << 0,
        is_external = 1 << 1,

        is_pointer_type = 1 << 8,
        is_array_type = 1 << 9,
        is_signature_type = 1 << 10,

        is_generated_type = (is_pointer_type | is_array_type | is_signature_type),
    };

    inline constexpr type_flags operator|(type_flags lhs, type_flags rhs) noexcept
    {
        return type_flags(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }
    inline constexpr type_flags& operator|=(type_flags& lhs, type_flags rhs) noexcept
    {
        lhs = lhs | rhs;
        return lhs;
    }
    inline constexpr bool operator&(type_flags lhs, type_flags rhs) noexcept
    {
        return type_flags(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs)) != type_flags::none;
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
        string_offset(uint32_t offset, uint32_t length) :
            offset(offset),
            length(length) {}

        uint32_t offset;
        uint32_t length;
    };

    template<typename value_t, size_t size> class handle
    {
        handle(const handle&) = delete;
        handle& operator=(const handle&) = delete;

        /*
            *** Experimental stack value pimpl ***
            
            All of propane's front-end headers hide the implementation.
            Normal Pimpl patterns allocate the implementation on the heap and store a pointer in the
            front-end class. This means that everytime the implementation is accessed, the pointer
            has to be dereferenced.
            
            This (experimental) pattern adds a block of bytes to the front-end class big enough to
            fit the implementation class. The implementation class is constructed in-place using
            emplacement new, and destructed at the end of the front-end class lifetime.
            
            The risk is that depending on platform and architecture, the implementation class sizes may
            vary greatly. Buffer sizes need to be big enough to fit most cases, but this adds additional
            (potentially unused) space to the front-end class. It also may cause issues when compiling
            across different ABI's. Generally, propane is included into projects by source and compiled
            along with the software, this should not be a problem.
            
            Setting the following define to 0 disables this feature and replaces it a regular new-allocated pimpl:
        */
#define USE_STACK_VALUE_PIMPL 1

#if USE_STACK_VALUE_PIMPL
    protected:
        template<typename... args> handle(args&... arg)
        {
            static_assert(sizeof(value_t) <= size, "Value type size greater than buffer size");
            new (reinterpret_cast<value_t*>(data.bytes)) value_t(arg...);
        }
        ~handle()
        {
            reinterpret_cast<value_t*>(data.bytes)->~value_t();
            std::memset(data.bytes, 0, size);
        }

        inline value_t& self() noexcept
        {
            return reinterpret_cast<value_t&>(data);
        }
        inline const value_t& self() const noexcept
        {
            return reinterpret_cast<const value_t&>(data);
        }

    private:
        struct { uint8_t bytes[size]; } data;
#else
    protected:
        template<typename... args> handle(args&... arg)
        {
            handle_ptr = new value_t(arg...);
        }
        ~handle()
        {
            delete handle_ptr;
            handle_ptr = nullptr;
        }

        inline value_t& self() noexcept
        {
            return *handle_ptr;
        }
        inline const value_t& self() const noexcept
        {
            return *handle_ptr;
        }

    private:
        value_t* handle_ptr;
#endif
    };

    // Span utility
    template<typename value_t> inline span<const value_t> init_span(std::initializer_list<value_t> init) noexcept
    {
        return span<const value_t>(init.begin(), init.size());
    }
    template<typename value_t> inline span<const value_t> init_span(std::initializer_list<const value_t> init) noexcept
    {
        return span<const value_t>(init.begin(), init.size());
    }
    template<typename value_t> inline span<const value_t> init_span(value_t& init) noexcept
    {
        return span<const value_t>(&init, 1);
    }
    template<typename value_t> inline span<const value_t> init_span(const value_t& init) noexcept
    {
        return span<const value_t>(&init, 1);
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