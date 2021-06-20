#ifndef _HEADER_PROPANE_RUNTIME
#define _HEADER_PROPANE_RUNTIME

#include "propane_common.hpp"
#include "propane_block.hpp"

namespace propane
{
    // Field
    // Contains info regarding struct fields
    struct field
    {
        field() = default;
        field(name_idx name, type_idx type, size_t offset = 0) :
            name(name),
            type(type),
            offset(offset) {}

        // Field name
        name_idx name;
        // Field type
        type_idx type;
        // Byte offset in the struct (relative to front of struct)
        aligned_size_t offset;
    };

    // Stackvar
    // Contains info regarding stack variables or parameters
    struct stackvar
    {
        stackvar() = default;
        stackvar(type_idx type, size_t offset = 0) :
            type(type),
            offset(offset) {}

        // Variable type
        type_idx type;
        // Byte offset on the stack (relative to front of stack)
        aligned_size_t offset;
    };

    // Generate type information
    // Contains information regarding generated types
    struct generated_type
    {
        struct pointer_data
        {
            pointer_data() = default;
            pointer_data(type_idx underlying_type, size_t underlying_size) :
                underlying_type(underlying_type),
                underlying_size(underlying_size) {}

            // Underlying type index
            type_idx underlying_type;
            // Underlying type size (for pointer arithmetics)
            aligned_size_t underlying_size;
        };

        struct array_data
        {
            array_data() = default;
            array_data(type_idx underlying_type, size_t array_size) :
                underlying_type(underlying_type),
                array_size(array_size) {}

            // Underlying type index
            type_idx underlying_type;
            // Array element count (number of items, not byte size)
            aligned_size_t array_size;
        };

        struct signature_data
        {
            signature_data() = default;
            signature_data(signature_idx index) :
                index(index),
                zero(0) {}

            // Index to the signature of this method pointer
            signature_idx index;

        private:
            // For initialization
            aligned_size_t zero;
        };

        generated_type() = default;
        generated_type(int32_t) :
            pointer(type_idx(0), 0) {}
        generated_type(const pointer_data& pointer) :
            pointer(pointer) {}
        generated_type(const array_data& array) :
            array(array) {}
        generated_type(const signature_data& signature) :
            signature(signature) {}

        union
        {
            // This data is valid if type is a pointer
            pointer_data pointer;
            // This data is valid if type is an array
            array_data array;
            // This data is valid if type is a signature
            signature_data signature;
        };
    };

    // Optional meta data that gets included per type/method,
    // if set during generation, this data will contain the filename
    // and line number where the type/method was originally defined.
    // If not set during generation, index will equal to meta_idx::invalid
    struct metadata
    {
        meta_idx index;
        index_t line_number;
    };

    // Type definition
    struct type
    {
        // Name (invalid for generated types)
        name_idx name;
        // Unique index
        type_idx index;
        // Flags (see helper functions below)
        type_flags flags;
        // Generated type information
        // (only valid if this type is a generated type)
        generated_type generated;
        // List of fields
        static_block<field> fields;
        // Total type size (in bytes)
        aligned_size_t total_size;
        // Index to the pointer type that uses this type as underlying
        // This is optional, some types might not have need for a pointer type
        type_idx pointer_type;
        // Metadata
        metadata meta;

        inline bool is_integral() const noexcept
        {
            return propane::is_integral(index);
        }
        inline bool is_floating_point() const noexcept
        {
            return propane::is_floating_point(index);
        }
        inline bool is_arithmetic() const noexcept
        {
            return propane::is_arithmetic(index);
        }

        inline bool is_pointer() const noexcept
        {
            return flags & type_flags::is_pointer_type;
        }
        inline bool is_array() const noexcept
        {
            return flags & type_flags::is_array_type;
        }
        inline bool is_signature() const noexcept
        {
            return flags & type_flags::is_signature_type;
        }
        inline bool is_generated() const noexcept
        {
            return flags & type_flags::is_generated_type;
        }

        inline bool is_struct() const noexcept
        {
            return !is_arithmetic() && !is_generated();
        }
        inline bool is_union() const noexcept
        {
            return flags & type_flags::is_union;
        }
    };

    // Method signature
    // Contains information required for invoking methods
    struct signature
    {
        // Unique index
        signature_idx index;
        // Return type (voidtype if none)
        type_idx return_type;
        // List of parameters (and their byte offsets)
        static_block<stackvar> parameters;
        // Total size of parameter list in bytes
        aligned_size_t parameters_size;

        inline bool has_return_value() const noexcept
        {
            return return_type != type_idx::voidtype;
        }
    };

    // Method definition
    struct method
    {
        // Name
        name_idx name;
        // Unique index
        method_idx index;
        // Flags (see helper functions below)
        type_flags flags;
        // Signature index
        signature_idx signature;
        // Actual instruction bytecode
        static_block<uint8_t> bytecode;
        // Label locations (byte offset relative to start of bytecode)
        static_block<aligned_size_t> labels;
        // Stack variables
        static_block<stackvar> stackvars;
        // Total stack variable size
        aligned_size_t stack_size;
        // Metadata
        metadata meta;

        inline bool is_external() const noexcept
        {
            return flags & type_flags::is_external;
        }
    };

    // Field address
    // Contains the set of field names required for accessing
    // nested fields. This can be important for generators when
    // unwinding nested structs.
    struct field_address
    {
        // Type from which any field is initially accessed
        type_idx object_type;
        // Field name chain leading down to target field
        static_block<name_idx> field_names;
    };

    // Field offset
    // Contains information regarding field offsets. Interpeter runtime
    // utilizes this for fast lookup of field offsets, but this part of
    // information is probably useless to a generator.
    struct field_offset
    {
        // Field address (see above)
        field_address name;
        // Field type
        type_idx type;
        // Field offset (relative to field address root type)
        aligned_size_t offset;
    };

    // Data table
    // Contains global data
    struct data_table
    {
        // List of names and offsets per global
        // Offset is relative to front of data array
        indexed_static_block<global_idx, field> info;
        // Actual global data
        static_block<uint8_t> data;
    };

    // String table
    // Tightly packed container for strings
    template<typename key_t> struct string_table
    {
        // String info (offset and length)
        static_block<string_offset> entries;
        // String character data
        static_block<char> strings;

        inline std::string_view operator[](key_t key) const noexcept
        {
            if (is_valid_index(key))
            {
                const auto& entry = entries[size_t(key)];
                return std::string_view(strings.data() + entry.offset, entry.length);
            }
            return std::string_view();
        }
        inline bool is_valid_index(key_t key) const noexcept
        {
            return size_t(key) < entries.size();
        }
    };

    // Actual assembly data
    // Contains all types, methods, signatures and offsets required to
    // A) Generate a program in any programming language or assembler
    //      If this is the case, information regarding names and strings is
    //      more relevant than data offsets.
    // B) Execute directly in an interpreter
    //      If this is the case, information regarding data offsets
    //      is more relevant than names and fields.
    struct assembly_data
    {
        // List of types
        indexed_static_block<type_idx, type> types;
        // List of methods
        indexed_static_block<method_idx, method> methods;
        // List of signatures
        indexed_static_block<signature_idx, signature> signatures;
        // List of offsets
        indexed_static_block<offset_idx, field_offset> offsets;

        // Global
        data_table globals;
        // Constant data
        data_table constants;

        // Database of type/method/field names
        string_table<name_idx> database;
        // Database of type/method meta info
        string_table<meta_idx> metatable;
        // Index of main entry point method
        // (method_idx::invalid if none was provided)
        method_idx main;
        // Runtime hash for validation checking
        aligned_size_t runtime_hash;

        // Utility function for generating a full typename.
        // Generated type names don't get exported into the database,
        // so this function can help generating a typename for debugging purposes.
        void generate_name(type_idx type, std::string& out_name) const;
    };

    struct runtime_parameters
    {
        runtime_parameters() :
            max_stack_size(1 << 20),
            min_stack_size(1 << 15),
            max_callstack_depth(1024) {}

        size_t max_stack_size;
        size_t min_stack_size;
        size_t max_callstack_depth;
    };

    // Runtime object.
    // Contains a list of libraries with external function calls which can be invoked at runtime.
    // When executing an assembly, make sure the assembly was linked with the same version of the runtime.
    class runtime : public handle<class runtime_data, sizeof(size_t) * 40>
    {
    public:
        runtime(std::span<const class library> libs = std::span<const class library>());
        explicit runtime(const class library& lib);
        ~runtime();

        runtime& operator+=(const class library&);

        int32_t execute(const class assembly& linked_assembly, runtime_parameters parameters = runtime_parameters());

        size_t hash() const noexcept;

    private:
        friend class assembly_linker;
    };
}

#endif