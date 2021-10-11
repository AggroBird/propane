#ifndef _HEADER_PROPANE_GENERATOR
#define _HEADER_PROPANE_GENERATOR

#include "propane_intermediate.hpp"
#include "propane_runtime.hpp"

namespace propane
{
    struct address
    {
        address(index_t index, address_type type,
            address_modifier modifier = address_modifier::none,
            address_prefix prefix = address_prefix::none) :
            header(type, prefix, modifier, index),
            payload(0) {}

        address_header header;
        union address_payload
        {
            address_payload() = default;
            address_payload(uint64_t init) : u64(init) {}

            int8_t i8;
            uint8_t u8;
            int16_t i16;
            uint16_t u16;
            int32_t i32;
            uint32_t u32;
            int64_t i64;
            uint64_t u64;
            float f32;
            double f64;
            void* vptr;
            name_idx global;
            offset_idx field;
            offset_t offset;
        } payload;

        inline bool operator==(const address& other) const noexcept
        {
            return header == other.header && payload.u64 == other.payload.u64;
        }
        inline bool operator!=(const address& other) const noexcept
        {
            return header != other.header || payload.u64 != other.payload.u64;
        }
    };

    struct constant : public address
    {
    private:
        constant(type_idx type) : address(index_t(type), address_type::constant) {}

    public:
        constant(int8_t val) : constant(type_idx::i8) { payload.i8 = val; }
        constant(uint8_t val) : constant(type_idx::u8) { payload.u8 = val; }
        constant(int16_t val) : constant(type_idx::i16) { payload.i16 = val; }
        constant(uint16_t val) : constant(type_idx::u16) { payload.u16 = val; }
        constant(int32_t val) : constant(type_idx::i32) { payload.i32 = val; }
        constant(uint32_t val) : constant(type_idx::u32) { payload.u32 = val; }
        constant(int64_t val) : constant(type_idx::i64) { payload.i64 = val; }
        constant(uint64_t val) : constant(type_idx::u64) { payload.u64 = val; }
        constant(float val) : constant(type_idx::f32) { payload.f32 = val; }
        constant(double val) : constant(type_idx::f64) { payload.f64 = val; }
        constant(std::nullptr_t) : constant(type_idx::vptr) { payload.vptr = nullptr; }
        constant(name_idx val) : constant(type_idx::voidtype) { payload.global = val; }
    };

    struct prefixable_address : public address
    {
    protected:
        prefixable_address(index_t index, address_type type) :
            address(index, type) {}

    public:
        address operator*() const
        {
            address result = *this;
            result.header.set_prefix(address_prefix::indirection);
            return result;
        }
        address operator&() const
        {
            address result = *this;
            result.header.set_prefix(address_prefix::address_of);
            return result;
        }
        address operator!() const
        {
            address result = *this;
            result.header.set_prefix(address_prefix::size_of);
            return result;
        }
    };

    struct indirect_address : public prefixable_address
    {
        prefixable_address field(offset_idx field) const
        {
            prefixable_address result = *this;
            result.header.set_modifier(address_modifier::indirect_field);
            result.payload.field = field;
            return result;
        }
    };

    struct modifyable_address : public prefixable_address
    {
    protected:
        modifyable_address(index_t index, address_type type) :
            prefixable_address(index, type) {}

    public:
        prefixable_address field(offset_idx field) const
        {
            prefixable_address result = *this;
            result.header.set_modifier(address_modifier::direct_field);
            result.payload.field = field;
            return result;
        }
        const indirect_address* operator->() const
        {
            return reinterpret_cast<const indirect_address*>(this);
        }
        prefixable_address operator[](offset_t offset) const
        {
            prefixable_address result = *this;
            result.header.set_modifier(address_modifier::subscript);
            result.payload.offset = offset;
            return result;
        }
    };

    struct stack : public modifyable_address
    {
        stack(index_t index) :
            modifyable_address(index, address_type::stackvar) {}
    };

    struct param : public modifyable_address
    {
        param(index_t index) :
            modifyable_address(index, address_type::parameter) {}
    };

    struct retval : public modifyable_address
    {
        retval() :
            modifyable_address(address_header::index_max, address_type::stackvar) {}
    };

    struct global : public modifyable_address
    {
        global(name_idx name) :
            modifyable_address(index_t(name), address_type::global) {}
    };

    // Experimental Propane bytecode generator
    // Inherit from this to implement a parser
    class generator : public handle<class generator_impl, sizeof(size_t) * 128>
    {
    public:
        generator();
        // String name of the file (will be included in type/method metadata)
        generator(std::string_view name);
        ~generator();

        generator(const generator&) = delete;
        generator& operator=(const generator&) = delete;

        class type_writer final : public handle<class type_writer_impl, sizeof(size_t) * 32>
        {
        public:
            type_writer(const type_writer&) = delete;
            type_writer& operator=(const type_writer&) = delete;

            name_idx name() const;
            type_idx index() const;

            // Field declaration for structs
            void declare_field(type_idx type, name_idx name);
            void declare_field(type_idx type, std::string_view name);

            std::span<const field> fields() const;

            // Finalize
            void finalize();

        private:
            friend class generator_impl;
            friend class generator;
            friend class type_writer_impl;

            type_writer(class generator_impl&, name_idx, type_idx, bool);
            ~type_writer();

            file_meta get_meta() const;
        };

        class method_writer final : public handle<class method_writer_impl, sizeof(size_t) * 128>
        {
        public:
            method_writer(const method_writer&) = delete;
            method_writer& operator=(const method_writer&) = delete;

            name_idx name() const;
            method_idx index() const;

            // Variable stack
            void push(std::span<const type_idx> types);
            inline void push(std::initializer_list<type_idx> types)
            {
                push(init_span(types));
            }
            inline void push(type_idx type)
            {
                push(init_span(type));
            }

            std::span<const stackvar> stack() const;

            // Declare label for later use
            label_idx declare_label(std::string_view label_name);
            // Write label (this should be called only once per label)
            void write_label(label_idx label);

            // Instruction writer methods
            void write_noop();

            void write_set(address lhs, address rhs);
            void write_conv(address lhs, address rhs);

            void write_not(address lhs);
            void write_neg(address lhs);
            void write_mul(address lhs, address rhs);
            void write_div(address lhs, address rhs);
            void write_mod(address lhs, address rhs);
            void write_add(address lhs, address rhs);
            void write_sub(address lhs, address rhs);
            void write_lsh(address lhs, address rhs);
            void write_rsh(address lhs, address rhs);
            void write_and(address lhs, address rhs);
            void write_xor(address lhs, address rhs);
            void write_or(address lhs, address rhs);

            void write_padd(address lhs, address rhs);
            void write_psub(address lhs, address rhs);
            void write_pdif(address lhs, address rhs);

            void write_cmp(address lhs, address rhs);
            void write_ceq(address lhs, address rhs);
            void write_cne(address lhs, address rhs);
            void write_cgt(address lhs, address rhs);
            void write_cge(address lhs, address rhs);
            void write_clt(address lhs, address rhs);
            void write_cle(address lhs, address rhs);
            void write_cze(address lhs);
            void write_cnz(address lhs);

            void write_br(label_idx label);
            void write_beq(label_idx label, address lhs, address rhs);
            void write_bne(label_idx label, address lhs, address rhs);
            void write_bgt(label_idx label, address lhs, address rhs);
            void write_bge(label_idx label, address lhs, address rhs);
            void write_blt(label_idx label, address lhs, address rhs);
            void write_ble(label_idx label, address lhs, address rhs);
            void write_bze(label_idx label, address lhs);
            void write_bnz(label_idx label, address lhs);

            void write_sw(address addr, std::span<const label_idx> labels);
            inline void write_sw(address addr, std::initializer_list<label_idx> labels)
            {
                write_sw(addr, init_span(labels));
            }

            void write_call(method_idx method, std::span<const address> args = std::span<const address>());
            inline void write_call(method_idx method, std::initializer_list<address> args)
            {
                write_call(method, init_span(args));
            }
            void write_callv(address addr, std::span<const address> args = std::span<const address>());
            inline void write_callv(address addr, std::initializer_list<address> args)
            {
                write_callv(addr, init_span(args));
            }
            void write_ret();
            void write_retv(address addr);

            void write_dump(address addr);

            // Finalize
            void finalize();

        private:
            friend class generator_impl;
            friend class generator;
            friend class method_writer_impl;

            method_writer(class generator_impl&, name_idx, method_idx, signature_idx);
            ~method_writer();
            
            file_meta get_meta() const;
        };

        // Declare a unique identifier. If 'name' has already been used,
        // this method will return the same index
        name_idx make_identifier(std::string_view name);

        // Declare signatures
        // Signatures can be used for method declaration or signature type declaration
        signature_idx make_signature(type_idx return_type, std::span<const type_idx> parameter_types = std::span<const type_idx>());
        inline signature_idx make_signature(type_idx return_type, std::initializer_list<type_idx> parameter_types)
        {
            return make_signature(return_type, init_span(parameter_types));
        }

        // Offsets
        // Data views into structs relative to the root type.
        // Modifying field values is only possible through offsets.
        offset_idx make_offset(type_idx type, std::span<const name_idx> fields);
        inline offset_idx make_offset(type_idx type, std::initializer_list<name_idx> fields)
        {
            return make_offset(type, init_span(fields));
        }
        inline offset_idx make_offset(type_idx type, name_idx field)
        {
            return make_offset(type, init_span(field));
        }

        // Global and constant definition
        void define_global(name_idx name, bool is_constant, type_idx type, std::span<const constant> values = std::span<const constant>());
        inline void define_global(name_idx name, bool is_constant, type_idx type, std::initializer_list<constant> values)
        {
            define_global(name, is_constant, type, init_span(values));
        }
        inline void define_global(std::string_view name, bool is_constant, type_idx type, std::span<const constant> values = std::span<const constant>())
        {
            return define_global(make_identifier(name), is_constant, type, values);
        }
        inline void define_global(std::string_view name, bool is_constant, type_idx type, std::initializer_list<constant> values)
        {
            return define_global(make_identifier(name), is_constant, type, init_span(values));
        }

        // Type declaration
        // Can be called multiple times, but will always
        // return the same index for identifier 'name'
        type_idx declare_type(name_idx name);
        inline type_idx declare_type(std::string_view name)
        {
            return declare_type(make_identifier(name));
        }
        // Define type can only be called once
        type_writer& define_type(type_idx type, bool is_union = false);
        inline type_writer& define_type(std::string_view name, bool is_union = false)
        {
            return define_type(declare_type(make_identifier(name)));
        }

        // Creation of generated types
        type_idx declare_pointer_type(type_idx base_type);
        type_idx declare_array_type(type_idx base_type, size_t array_size);
        type_idx declare_signature_type(signature_idx signature);

        // Method declaration
        // Can be called multiple times, but will always
        // return the same index for identifier 'name'
        method_idx declare_method(name_idx name);
        inline method_idx declare_method(std::string_view name)
        {
            return declare_method(make_identifier(name));
        }
        // Define method can only be called once
        method_writer& define_method(method_idx method, signature_idx signature);
        inline method_writer& define_method(std::string_view name, signature_idx signature)
        {
            return define_method(declare_method(make_identifier(name)), signature);
        }

        // Set a line number to be included in type/method metadata
        void set_line_number(index_t line_number) noexcept;

        // Finalize
        // This method finishes up all the writers and releases all the resources.
        // The returned intermediate can be merged with other intermediates or linked and executed.
        intermediate finalize();

        file_meta get_meta() const;

    private:
        friend class generator_impl;
    };
}

#endif