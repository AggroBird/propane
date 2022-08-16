#ifndef _HEADER_ASSEMBLY_DATA
#define _HEADER_ASSEMBLY_DATA

#include "runtime.hpp"
#include "database.hpp"
#include "intermediate_data.hpp"

namespace propane
{
    class asm_type : public gen_type
    {
    public:
        asm_type() = default;
        asm_type(gen_type&& base) : gen_type(std::move(base)) {}
    };

    class asm_method : public gen_method
    {
    public:
        asm_method() = default;
        asm_method(gen_method&& base) : gen_method(std::move(base)) {}
    };

    class asm_signature : public gen_signature
    {
    public:
        asm_signature() = default;
        asm_signature(gen_signature&& base) : gen_signature(std::move(base)) {}
    };

    class asm_data_table : public gen_data_table
    {
    public:
        asm_data_table() = default;
        asm_data_table(gen_data_table&& base) : gen_data_table(std::move(base)) {}
    };

    class asm_field_address : public gen_field_address
    {
    public:
        asm_field_address() = default;
        asm_field_address(gen_field_address&& base) : gen_field_address(std::move(base)) {}
    };

    class asm_field_offset
    {
    public:
        asm_field_offset() = default;
        asm_field_offset(gen_field_offset&& base) :
            name(std::move(base.name)),
            type(base.type),
            offset(base.offset) {}

        asm_field_address name;
        type_idx type;
        size_t offset;
    };

    class asm_database : public gen_database
    {
    public:
        asm_database() = default;
        asm_database(gen_database&& base) : gen_database(std::move(base)) {}
    };

    class asm_metatable : public gen_metatable
    {
    public:
        asm_metatable() = default;
        asm_metatable(gen_metatable&& base) : gen_metatable(std::move(base)) {}
    };

    class asm_assembly_data
    {
    public:
        MOVABLE_CLASS_DEFAULT(asm_assembly_data) = default;

        static void serialize(assembly& dst, const asm_assembly_data& data);

        indexed_vector<type_idx, asm_type> types;
        indexed_vector<method_idx, asm_method> methods;
        indexed_vector<signature_idx, asm_signature> signatures;
        indexed_vector<offset_idx, asm_field_offset> offsets;

        asm_data_table globals;
        asm_data_table constants;

        asm_database database;
        asm_metatable metatable;
        method_idx main = method_idx::invalid;
        size_t runtime_hash = 0;

        inline file_meta make_meta(type_idx type) const noexcept
        {
            const auto& meta = types[type].meta;
            return file_meta(metatable[meta.index].name, meta.line_number);
        }
        inline file_meta make_meta(method_idx method) const noexcept
        {
            const auto& meta = methods[method].meta;
            return file_meta(metatable[meta.index].name, meta.line_number);
        }
    };

    using name_string_table_t = string_table<name_idx>;
    CUSTOM_SERIALIZER(asm_database, name_string_table_t)
    {
        inline static void write(block_writer & writer, const asm_database & value)
        {
            value.serialize_string_table(writer);
        }
        inline static void read(const void*& data, asm_database & value)
        {
            throw 0;
        }
    };

    using meta_string_table_t = string_table<meta_idx>;
    CUSTOM_SERIALIZER(asm_metatable, meta_string_table_t)
    {
        inline static void write(block_writer & writer, const asm_metatable & value)
        {
            value.serialize_string_table(writer);
        }
        inline static void read(const void*& data, asm_metatable & value)
        {
            throw 0;
        }
    };

    SERIALIZABLE_PAIR(asm_type, type, name, index, flags, generated, fields, total_size, pointer_type, meta);
    SERIALIZABLE_PAIR(asm_signature, signature, index, return_type, parameters, parameters_size);
    SERIALIZABLE_PAIR(asm_method, method, name, index, flags, signature, bytecode, labels, stackvars, method_stack_size, total_stack_size, meta);
    SERIALIZABLE_PAIR(asm_field_address, field_address, object_type, field_names);
    SERIALIZABLE_PAIR(asm_field_offset, field_offset, name, type, offset);
    SERIALIZABLE_PAIR(asm_data_table, data_table, info, data);
    SERIALIZABLE_PAIR(asm_assembly_data, assembly_data, types, methods, signatures, offsets, globals, constants, database, metatable, main, runtime_hash);
}

#endif