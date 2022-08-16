#ifndef _HEADER_INTERMEDIATE_DATA
#define _HEADER_INTERMEDIATE_DATA

#include "generation.hpp"
#include "propane_intermediate.hpp"

namespace propane
{
    // Serialization
    struct im_type : public type
    {

    };

    struct im_signature : public signature
    {

    };

    struct im_method : public method
    {
        static_block<method_idx> calls;
        static_block<translate_idx> globals;
        static_block<offset_idx> offsets;
    };

    struct im_field_address : public field_address
    {

    };

    struct im_field_offset
    {
        im_field_address name;
        type_idx type;
        aligned_size_t offset;
    };

    struct im_data_table : public data_table
    {

    };

    struct im_assembly_data
    {
        indexed_static_block<type_idx, im_type> types;
        indexed_static_block<method_idx, im_method> methods;
        indexed_static_block<signature_idx, im_signature> signatures;
        indexed_static_block<offset_idx, im_field_offset> offsets;

        im_data_table globals;
        im_data_table constants;

        static_database<name_idx, lookup_idx> database;
        static_database<meta_idx, void> metatable;
    };

    using static_lookup_database_t = static_database<name_idx, lookup_idx>;
    CUSTOM_SERIALIZER(gen_database, static_lookup_database_t)
    {
        inline static void write(block_writer & writer, const gen_database & value)
        {
            value.serialize_database(writer);
        }
        inline static void read(const void*& data, gen_database & value)
        {
            value.deserialize_database(*reinterpret_cast<const static_lookup_database_t*&>(data)++);
        }
    };

    using static_meta_database_t = static_database<meta_idx, void>;
    CUSTOM_SERIALIZER(gen_metatable, static_meta_database_t)
    {
        inline static void write(block_writer & writer, const gen_metatable & value)
        {
            value.serialize_database(writer);
        }
        inline static void read(const void*& data, gen_metatable & value)
        {
            value.deserialize_database(*reinterpret_cast<const static_meta_database_t*&>(data)++);
        }
    };

    SERIALIZABLE_PAIR(gen_type, im_type, name, index, flags, generated, fields, total_size, pointer_type, meta);
    SERIALIZABLE_PAIR(gen_signature, im_signature, index, return_type, parameters, parameters_size);
    SERIALIZABLE_PAIR(gen_method, im_method, name, index, flags, signature, bytecode, labels, stackvars, method_stack_size, total_stack_size, calls, globals, offsets, meta);
    SERIALIZABLE_PAIR(gen_field_address, im_field_address, object_type, field_names);
    SERIALIZABLE_PAIR(gen_field_offset, im_field_offset, name, type, offset);
    SERIALIZABLE_PAIR(gen_data_table, im_data_table, info, data);
    SERIALIZABLE_PAIR(gen_intermediate_data, im_assembly_data, types, methods, signatures, offsets, globals, constants, database, metatable);
}

#endif