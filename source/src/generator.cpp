#include "propane_generator.hpp"
#include "generation.hpp"
#include "errors.hpp"
#include "utility.hpp"

#define VALIDATE(errc, expr, ...) ENSURE_WITH_META(errc, this->get_meta(), expr, propane::generator_exception, __VA_ARGS__)

#define VALIDATE_IDENTIFIER(str) VALIDATE(ERRC::GNR_INVALID_IDENTIFIER, propane::is_identifier(str), \
    "Invalid identifier: '%'", str)
#define VALIDATE_PARAM_COUNT(num) VALIDATE(ERRC::GNR_PARAMETER_OVERFLOW, static_cast<size_t>(num) <= method_parameter_max, \
    "Method parameter count exceeds maximum (%/%)", num, method_parameter_max)
#define VALIDATE_INIT_COUNT(num) VALIDATE(ERRC::GNR_INITIALIZER_OVERFLOW, static_cast<size_t>(num) <= global_initializer_max, \
    "Constant initializer count exceeds maximum (%/%)", num, global_initializer_max)
#define VALIDATE_INDEX_RANGE(idx, max) VALIDATE(ERRC::GNR_INDEX_OUT_OF_RANGE, static_cast<size_t>(idx) < static_cast<size_t>(max), \
    "% out of range (%/%)", get_index_type_name(idx), static_cast<size_t>(idx), max)
#define VALIDATE_ARRAY_LENGTH(len) VALIDATE(ERRC::GNR_ARRAY_LENGTH_ZERO, (len) != 0, \
    "Array length cannot be zero")
#define VALIDATE_INDEX_VALUE(idx) VALIDATE(ERRC::GNR_INVALID_INDEX, static_cast<size_t>(idx) != static_cast<size_t>(invalid_index), \
    "Invalid index provided")
#define VALIDATE_OFFSET_VALUE(empty) VALIDATE(ERRC::GNR_EMPTY_OFFSET, empty, \
    "Empty offset sequence provided")
#define VALIDATE_IDENTIFIER_TYPE(expr, lhs_type, rhs_type, name) VALIDATE(ERRC::GNR_IDENTIFIER_TYPE_MISMATCH, expr, \
    "Declaration of % '%' collides with previous % declaration", lhs_type, name, rhs_type)
#define VALIDATE_NONVOID(type) VALIDATE(ERRC::GNR_INVALID_VOID_TYPE, (type) != propane::type_idx::voidtype, \
    "Void type is not valid as a parameter or field type")
#define VALIDATE_TYPE_DEC(expr, type_name, type_meta) VALIDATE(ERRC::GNR_TYPE_REDECLARATION, expr, \
    "Type '%' has already been declared (see %)", type_name, type_meta)
#define VALIDATE_METHOD_DEC(expr, method_name, method_meta) VALIDATE(ERRC::GNR_METHOD_REDECLARATION, expr, \
    "Method '%' has already been declared (see %)", method_name, method_meta)
#define VALIDATE_GLOBAL_DEC(expr, global_name) VALIDATE(ERRC::GNR_GLOBAL_REDECLARATION, expr, \
    "Global '%' has already been declared", global_name)
#define VALIDATE_FIELD_DEC(expr, field_name, type_name, type_meta) VALIDATE(ERRC::GNR_FIELD_REDECLARATION, expr, \
    "Field '%' has already been declared (see declaration for '%' at %)", field_name, type_name, type_meta)
#define VALIDATE_STACK_DEC(expr, method_meta) VALIDATE(ERRC::GNR_STACK_REDECLARATION, expr, \
    "Stack for method '%' has already been set", method_meta)
#define VALIDATE_LABEL_DEC(expr, label_name) VALIDATE(ERRC::GNR_LABEL_REDECLARATION, expr, \
    "Label '%' has already been defined", label_name)
#define VALIDATE_LABEL_DEF(expr, label_name) VALIDATE(ERRC::GNR_LABEL_UNDEFINED, expr, \
    "Undefined label '%'", label_name)
#define VALIDATE_RET_VAL(expr, method_name, method_meta) VALIDATE(ERRC::GNR_INVALID_RET_VAL, expr, \
    "Method return value does not match declaration (see declaration for '%' at %)", method_name, method_meta)
#define VALIDATE_STACK_INDEX(idx, max) VALIDATE(ERRC::GNR_STACK_OUT_OF_RANGE, static_cast<size_t>(idx) < static_cast<size_t>(max), \
    "Stack index out of range (%/%)", static_cast<size_t>(idx), max)
#define VALIDATE_PARAM_INDEX(idx, max) VALIDATE(ERRC::GNR_PARAM_OUT_OF_RANGE, static_cast<size_t>(idx) < static_cast<size_t>(max), \
    "Parameter index out of range (%/%)", static_cast<size_t>(idx), max)
#define VALIDATE_NONCONST(expr) VALIDATE(ERRC::GNR_INVALID_CONSTANT, expr, \
    "Constant is not valid as left-hand side operand")
#define VALIDATE_HAS_RETURNED(expr, method_name, method_meta) VALIDATE(ERRC::GNR_MISSING_RET_VAL, expr, \
    "Method is expecting a return value (see declaration for '%' at %)", method_name, method_meta)
#define VALIDATE_CONST_ADDR(expr) VALIDATE(ERRC::GNR_INVALID_CONSTANT_ADDR, expr, \
    "Constant address cannot have modifiers or prefixes")

#define VALIDATE_INDEX(id, max) { VALIDATE_INDEX_VALUE(id); VALIDATE_INDEX_RANGE(id, max); }
#define VALIDATE_TYPE(id, max) { VALIDATE_INDEX(id, max); VALIDATE_NONVOID(id); }
#define VALIDATE_TYPES(set, max) { for(const auto& id : set) { VALIDATE_INDEX(id, max); VALIDATE_NONVOID(id); } }
#define VALIDATE_INDICES(set, max) { for(const auto& id : set) { VALIDATE_INDEX(id, max); } }

namespace propane
{
    static constexpr size_t method_parameter_max = 256;
    static constexpr size_t global_initializer_max = 65536;

    class generator_impl final : public gen_intermediate_data
    {
    public:
        NOCOPY_CLASS_DEFAULT(generator_impl);
        ~generator_impl();

        inline name_idx emplace_identifier(string_view identifier)
        {
            if (auto find = database.find(identifier))
            {
                return find.key;
            }
            else
            {
                return database.emplace(identifier, lookup_idx::make_identifier()).key;
            }
        }

        void define_data(lookup_type lookup, type_idx type, name_idx name, span<const constant> values)
        {
            VALIDATE_INDEX(name, database.size());
            VALIDATE_INDEX(type, types.size());
            VALIDATE_INIT_COUNT(values.size());

            auto find = database[name];
            VALIDATE_GLOBAL_DEC(find->lookup != lookup_type::global && find->lookup != lookup_type::constant, find.name);
            VALIDATE_IDENTIFIER_TYPE(find->lookup == lookup_type::identifier, lookup_type::identifier, find->lookup, find.name);

            // Validate values
            for (const auto& it : values)
            {
                const type_idx init_type = type_idx(it.header.index());
                if (init_type == type_idx::voidtype)
                    VALIDATE_INDEX(it.payload.global, database.size())
                else
                    ASSERT(init_type <= propane::type_idx::vptr, "")
            }

            auto& table = get_data_table(lookup);
            const uint32_t idx = uint32_t(table.info.size());

            // Upgrade to global
            *find = lookup_idx(lookup, idx);
            table.info.push_back(field(name, type, table.data.size()));

            append_bytecode(table.data, static_cast<uint16_t>(values.size()));
            for (const auto& it : values)
            {
                const type_idx init_type = type_idx(it.header.index());

                append_bytecode(table.data, static_cast<uint8_t>(init_type));
                if (init_type == type_idx::voidtype)
                {
                    // Constant identifier
                    append_bytecode(table.data, it.payload.global);
                }
                else if (init_type != type_idx::vptr)
                {
                    // Encoded constant
                    append_constant(table.data, it);
                }
            }
        }

        inline gen_data_table& get_data_table(lookup_type type)
        {
            switch (type)
            {
                case lookup_type::global: return globals;
                case lookup_type::constant: return constants;
            }
            ASSERT(false, "Invalid lookup type");
            return globals;
        }

        // Writer objects, get released in the deconstructor or in finalize
        indexed_vector<type_idx, generator::type_writer*> type_writers;
        indexed_vector<method_idx, generator::method_writer*> method_writers;

        // Meta index for the current intermediate (0 if defined)
        meta_idx meta_index = meta_idx::invalid;
        uint32_t line_number = 0;

        vector<uint8_t> keybuf;


        inline file_meta get_meta() const
        {
            return file_meta(metatable[meta_index].name, line_number);
        }

        void append_constant(vector<uint8_t>& buf, address addr)
        {
            switch (type_idx(addr.header.index()))
            {
                case type_idx::i8: append_bytecode(buf, addr.payload.i8); break;
                case type_idx::u8: append_bytecode(buf, addr.payload.u8); break;
                case type_idx::i16: append_bytecode(buf, addr.payload.i16); break;
                case type_idx::u16: append_bytecode(buf, addr.payload.u16); break;
                case type_idx::i32: append_bytecode(buf, addr.payload.i32); break;
                case type_idx::u32: append_bytecode(buf, addr.payload.u32); break;
                case type_idx::i64: append_bytecode(buf, addr.payload.i64); break;
                case type_idx::u64: append_bytecode(buf, addr.payload.u64); break;
                case type_idx::f32: append_bytecode(buf, addr.payload.f32); break;
                case type_idx::f64: append_bytecode(buf, addr.payload.f64); break;
                case type_idx::vptr: append_bytecode(buf, addr.payload.vptr); break;
                default: ASSERT(false, "Invalid type index provided");
            }
        }
    };
    constexpr size_t generator_impl_handle_size = approximate_handle_size(sizeof(generator_impl));


    class type_writer_impl final : public gen_type
    {
    public:
        NOCOPY_CLASS_DEFAULT(type_writer_impl, generator_impl&, name_idx name, type_idx index, bool is_union);
        ~type_writer_impl();

        generator_impl& gen;

        inline file_meta get_meta() const
        {
            return gen.get_meta();
        }
    };
    constexpr size_t type_writer_impl_handle_size = approximate_handle_size(sizeof(type_writer_impl));

    class method_writer_impl final : public gen_method
    {
    public:
        NOCOPY_CLASS_DEFAULT(method_writer_impl, generator_impl&, name_idx name, method_idx index, signature_idx signature);
        ~method_writer_impl();


        // Lookup tables, to prevent duplicate indices
        unordered_map<method_idx, uint32_t> call_lookup;
        unordered_map<name_idx, uint32_t> global_lookup;
        unordered_map<offset_idx, uint32_t> offset_index_lookup;

        // Labels
        unordered_map<label_idx, uint32_t> label_locations;
        unordered_map<label_idx, vector<uint32_t>> unresolved_branches;
        database<uint32_t, label_idx> named_labels;
        indexed_vector<label_idx, uint32_t> label_declarations; // Unnamed labels will be added to the declarations list as 'invalid_index'

        size_t parameter_count = 0;
        size_t last_return = 0;
        bool expects_return_value = false;

        generator_impl& gen;


        inline type_idx get_type(address addr) const
        {
            const uint32_t idx = addr.header.index();
            switch (addr.header.type())
            {
                case address_type::stackvar:
                {
                    ASSERT(is_valid_index(stackvars, idx), "Index out of range");
                    return stackvars[idx].type;
                }
                case address_type::parameter:
                {
                    const auto& sig = gen.signatures[signature];
                    ASSERT(is_valid_index(sig.parameters, idx), "Index out of range");
                    return sig.parameters[idx].type;
                }
                case address_type::global:
                {
                    ASSERT(gen.globals.info.is_valid_index(global_idx(idx)), "Index out of range");
                    return gen.globals.info[global_idx(idx)].type;
                }
                case address_type::constant:
                {
                    ASSERT(gen.constants.info.is_valid_index(global_idx(idx)), "Index out of range");
                    return gen.constants.info[global_idx(idx)].type;
                }
            }
            return type_idx::invalid;
        }

        void resolve_labels()
        {
            // Fetch all labels that have been referenced by a branch
            map<uint32_t, vector<label_idx>> write_labels;
            for (auto& branch : unresolved_branches)
            {
                auto label = label_locations.find(branch.first);
                if (label == label_locations.end())
                {
                    // Label location not known
                    const auto label_name_index = label_declarations[branch.first];
                    if (label_name_index == invalid_index)
                    {
                        VALIDATE_LABEL_DEF(false, static_cast<uint32_t>(branch.first));
                    }
                    else
                    {
                        VALIDATE_LABEL_DEF(false, named_labels[label_name_index].name);
                    }
                }
                write_labels[label->second].push_back(label->first);
            }

            // Export labels
            labels.reserve(write_labels.size());
            for (auto& label : write_labels)
            {
                for (auto& branch_index : label.second)
                {
                    auto locations = unresolved_branches.find(branch_index);
                    for (auto& offset : locations->second)
                    {
                        *reinterpret_cast<uint32_t*>(bytecode.data() + offset) = label.first;
                    }
                }
                if (label.first >= bytecode.size())
                {
                    const bool expected = !gen.signatures[signature].has_return_value();
                    VALIDATE_RET_VAL(expected, gen.database[name].name, gen.make_meta(index));
                    write_ret();
                }
                labels.push_back(label.first);
            }
        }

        inline void append_bytecode(const void* ptr, size_t len)
        {
            const uint8_t* bptr = reinterpret_cast<const uint8_t*>(ptr);
            bytecode.insert(bytecode.end(), bptr, bptr + len);
        }
        template<typename value_t> inline void append_bytecode(const value_t& val)
        {
            static_assert(std::is_trivial_v<value_t> && !std::is_same_v<string_view, value_t>, "Type must be trivially copyable");
            append_bytecode(&val, sizeof(value_t));
        }

        void write_subcode_zero()
        {
            append_bytecode(subcode(0));
        }

        bool validate_address(address addr) const
        {
            switch (addr.header.type())
            {
                case address_type::stackvar:
                {
                    if (addr.header.index() != address_header_constants::index_max)
                    {
                        VALIDATE_STACK_INDEX(addr.header.index(), stackvars.size());
                    }
                }
                break;

                case address_type::parameter:
                {
                    VALIDATE_PARAM_INDEX(addr.header.index(), parameter_count);
                }
                break;

                case address_type::constant:
                {
                    VALIDATE_NONCONST(false);
                }
                break;
            }

            return true;
        }
        bool validate_operand(address addr) const
        {
            if (addr.header.type() == address_type::constant)
            {
                VALIDATE_CONST_ADDR(addr.header.prefix() == address_prefix::none && addr.header.modifier() == address_modifier::none);
                return true;
            }

            return validate_address(addr);
        }
        bool validate_operands(span<const address> args) const
        {
            for (const auto& it : args)
            {
                if (!validate_operand(it))
                {
                    return false;
                }
            }

            return true;
        }

        void write_address(address addr)
        {
            address_data_t data(0);

            data.header = addr.header;

            switch (addr.header.type())
            {
                case address_type::global:
                {
                    const name_idx global_name = (name_idx)addr.header.index();
                    auto find = global_lookup.find(global_name);
                    if (find == global_lookup.end())
                    {
                        // New global
                        const uint32_t idx = uint32_t(globals.size());
                        global_lookup.emplace(global_name, idx);
                        globals.push_back(global_name);
                        data.header.set_index(idx);
                    }
                    else
                    {
                        data.header.set_index(uint32_t(find->second));
                    }
                }
                break;
            }

            switch (addr.header.modifier())
            {
                case address_modifier::none: break;

                case address_modifier::direct_field:
                case address_modifier::indirect_field:
                {
                    const offset_idx field = addr.payload.field;
                    auto find = offset_index_lookup.find(field);
                    if (find == offset_index_lookup.end())
                    {
                        // New offset
                        const uint32_t idx = uint32_t(offsets.size());
                        offset_index_lookup.emplace(field, idx);
                        offsets.push_back(field);
                        data.field = (offset_idx)idx;
                    }
                    else
                    {
                        data.field = (offset_idx)find->second;
                    }
                }
                break;

                case address_modifier::offset:
                {
                    data.offset = addr.payload.offset;
                }
                break;
            }

            append_bytecode(data);
        }
        void write_operand(address addr)
        {
            if (addr.header.type() == address_type::constant)
            {
                append_bytecode(addr.header);
                gen.append_constant(bytecode, addr);
                return;
            }

            write_address(addr);
        }

        void write_label(label_idx label)
        {
            auto branch = unresolved_branches.find(label);
            vector<uint32_t>& list = branch == unresolved_branches.end() ? unresolved_branches.emplace(label, vector<uint32_t>()).first->second : branch->second;
            list.push_back(uint32_t(bytecode.size()));

            append_bytecode(uint32_t(0));
        }

        void write_expression(opcode op, address lhs)
        {
            if (validate_address(lhs))
            {
                append_bytecode(op);
                write_address(lhs);
            }
        }
        void write_expression(opcode op, address lhs, address rhs)
        {
            if (validate_address(lhs) && validate_operand(rhs))
            {
                append_bytecode(op);
                write_address(lhs);
                write_operand(rhs);
            }
        }

        void write_sub_expression(opcode op, address lhs)
        {
            if (validate_address(lhs))
            {
                append_bytecode(op);
                write_subcode_zero();
                write_address(lhs);
            }
        }
        void write_sub_expression(opcode op, address lhs, address rhs)
        {
            if (validate_address(lhs) && validate_operand(rhs))
            {
                append_bytecode(op);
                write_subcode_zero();
                write_address(lhs);
                write_operand(rhs);
            }
        }

        void write_branch(opcode op, label_idx label)
        {
            VALIDATE_INDEX(label, label_declarations.size());

            append_bytecode(op);
            write_label(label);
        }
        void write_branch(opcode op, label_idx label, address lhs)
        {
            VALIDATE_INDEX(label, label_declarations.size());
            if (validate_address(lhs))
            {
                append_bytecode(op);
                write_label(label);
                write_subcode_zero();
                write_address(lhs);
            }
        }
        void write_branch(opcode op, label_idx label, address lhs, address rhs)
        {
            VALIDATE_INDEX(label, label_declarations.size());
            if (validate_address(lhs) && validate_operand(rhs))
            {
                append_bytecode(op);
                write_label(label);
                write_subcode_zero();
                write_address(lhs);
                write_operand(rhs);
            }
        }

        void write_sw(address addr, span<const label_idx> switch_labels)
        {
            VALIDATE_ARRAY_LENGTH(switch_labels.size());
            VALIDATE_INDICES(switch_labels, label_declarations.size());
            if (validate_address(addr))
            {
                append_bytecode(opcode::sw);
                write_address(addr);

                append_bytecode(static_cast<uint32_t>(switch_labels.size()));
                for (auto it : switch_labels)
                {
                    write_label(it);
                }
            }
        }

        void write_call(method_idx method, span<const address> args)
        {
            VALIDATE_INDEX(method, gen.methods.size());
            VALIDATE_PARAM_COUNT(args.size());
            if (validate_operands(args))
            {
                append_bytecode(opcode::call);
                uint32_t idx;
                auto find = call_lookup.find(method);
                if (find == call_lookup.end())
                {
                    idx = uint32_t(calls.size());
                    calls.push_back(method);
                    call_lookup.emplace(method, idx);
                }
                else
                {
                    idx = find->second;
                }
                append_bytecode(idx);

                append_bytecode(static_cast<uint8_t>(args.size()));
                for (const auto& it : args)
                {
                    write_subcode_zero();
                    write_operand(it);
                }
            }
        }
        void write_callv(address addr, span<const address> args)
        {
            VALIDATE_PARAM_COUNT(args.size());
            if (validate_address(addr) && validate_operands(args))
            {
                append_bytecode(opcode::callv);
                write_address(addr);

                append_bytecode(static_cast<uint8_t>(args.size()));
                for (const auto& it : args)
                {
                    write_subcode_zero();
                    write_operand(it);
                }
            }
        }
        void write_ret()
        {
            const bool expected = !gen.signatures[signature].has_return_value();
            VALIDATE_RET_VAL(expected, gen.database[name].name, gen.make_meta(index));

            append_bytecode(opcode::ret);

            last_return = bytecode.size();
        }
        void write_retv(address addr)
        {
            const bool expected = gen.signatures[signature].has_return_value();
            VALIDATE_RET_VAL(expected, gen.database[name].name, gen.make_meta(index));
            if (validate_operand(addr))
            {
                append_bytecode(opcode::retv);
                write_subcode_zero();
                write_operand(addr);
            }

            last_return = bytecode.size();
        }

        void write_dump(address addr)
        {
            if (validate_operand(addr))
            {
                append_bytecode(opcode::dump);
                write_operand(addr);
            }
        }

        inline file_meta get_meta() const
        {
            return gen.get_meta();
        }
    };
    constexpr size_t method_writer_impl_handle_size = approximate_handle_size(sizeof(method_writer_impl));


    // Types
    type_writer_impl::type_writer_impl(generator_impl& gen, name_idx name, type_idx index, bool is_union) :
        gen_type(name, index),
        gen(gen)
    {
        flags |= extended_flags::is_defined;
        if (is_union) flags |= type_flags::is_union;

        meta.index = gen.meta_index;
        meta.line_number = gen.line_number;
    }
    type_writer_impl::~type_writer_impl()
    {

    }

    generator::type_writer::type_writer(generator_impl& gen, name_idx name, type_idx index, bool is_union) :
        handle(gen, name, index, is_union)
    {

    }
    generator::type_writer::~type_writer()
    {

    }

    name_idx generator::type_writer::name() const
    {
        return self().name;
    }
    type_idx generator::type_writer::index() const
    {
        return self().index;
    }

    void generator::type_writer::declare_field(type_idx type, name_idx name)
    {
        auto& writer = self();
        auto& gen = writer.gen;

        VALIDATE_TYPE(type, gen.types.size());
        VALIDATE_INDEX(name, gen.database.size());

        for (auto& f : writer.fields)
        {
            VALIDATE_FIELD_DEC(name != f.name, gen.database[name].name, gen.database[writer.name].name, gen.make_meta(writer.index));
        }

        writer.fields.push_back(field(name, type));
    }
    name_idx generator::type_writer::declare_field(type_idx type, string_view name)
    {
        auto& writer = self();
        auto& gen = writer.gen;

        VALIDATE_IDENTIFIER(name);
        VALIDATE_TYPE(type, gen.types.size());

        const name_idx id = gen.emplace_identifier(name);
        declare_field(type, id);
        return id;
    }
    span<const field> generator::type_writer::fields() const
    {
        return self().fields;
    }

    void generator::type_writer::finalize()
    {
        auto& writer = self();
        auto& gen = writer.gen;

        auto& dst = gen.types[writer.index];
        auto& src = writer;

        // Copy over pointer and array types (they get assigned by the generator)
        ASSERT(src.pointer_type == type_idx::invalid, "Pointer type index is not valid here");
        ASSERT(src.array_types.empty(), "Array type indices are not valid here");
        src.pointer_type = dst.pointer_type;
        std::swap(src.array_types, dst.array_types);

        dst = std::move(src);
        gen.type_writers[writer.index] = nullptr;

        delete this;
    }


    // Methods
    method_writer_impl::method_writer_impl(generator_impl& gen, name_idx name, method_idx index, signature_idx signature) :
        gen_method(name, index),
        gen(gen)
    {
        flags |= extended_flags::is_defined;
        this->signature = signature;
        const auto& sig = gen.signatures[signature];
        this->parameter_count = sig.parameters.size();
        this->expects_return_value = sig.has_return_value();

        meta.index = gen.meta_index;
        meta.line_number = gen.line_number;
    }
    method_writer_impl::~method_writer_impl()
    {

    }

    generator::method_writer::method_writer(class generator_impl& gen, name_idx name, method_idx index, signature_idx signature) :
        handle(gen, name, index, signature)
    {

    }
    generator::method_writer::~method_writer()
    {

    }

    name_idx generator::method_writer::name() const
    {
        return self().name;
    }
    method_idx generator::method_writer::index() const
    {
        return self().index;
    }

    void generator::method_writer::push(span<const type_idx> types)
    {
        auto& writer = self();

        VALIDATE_TYPES(types, writer.gen.types.size());

        writer.stackvars.reserve(writer.stackvars.size() + types.size());
        for (auto it : types)
        {
            writer.stackvars.push_back(stackvar(it));
        }
    }
    span<const stackvar> generator::method_writer::stack() const
    {
        return self().stackvars;
    }

    label_idx generator::method_writer::declare_label(string_view label_name)
    {
        VALIDATE_IDENTIFIER(label_name);

        auto& writer = self();

        auto find = writer.named_labels.find(label_name);
        if (!find)
        {
            // New named label
            const label_idx next = label_idx(writer.label_declarations.size());
            writer.label_declarations.push_back(writer.named_labels.emplace(label_name, next).key);
            return next;
        }
        return *find;
    }
    label_idx generator::method_writer::declare_label()
    {
        auto& writer = self();

        // New unnamed label
        const label_idx next = label_idx(writer.label_declarations.size());
        writer.label_declarations.push_back(invalid_index);
        return next;
    }
    void generator::method_writer::write_label(label_idx label)
    {
        auto& writer = self();

        VALIDATE_INDEX(label, writer.label_declarations.size());

        const auto find = writer.label_locations.find(label);
        if (find != writer.label_locations.end())
        {
            const auto label_name_index = writer.label_declarations[label];
            if (label_name_index == invalid_index)
            {
                VALIDATE_LABEL_DEC(false, static_cast<uint32_t>(label));
            }
            else
            {
                VALIDATE_LABEL_DEC(false, writer.named_labels[label_name_index].name);
            }
        }

        writer.label_locations.emplace(label, uint32_t(writer.bytecode.size()));
    }

    void generator::method_writer::write_noop()
    {
        self().append_bytecode(opcode::noop);
    }

    void generator::method_writer::write_set(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::set, lhs, rhs);
    }
    void generator::method_writer::write_conv(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::conv, lhs, rhs);
    }

    void generator::method_writer::write_not(address lhs)
    {
        self().write_sub_expression(opcode::ari_not, lhs);
    }
    void generator::method_writer::write_neg(address lhs)
    {
        self().write_sub_expression(opcode::ari_neg, lhs);
    }
    void generator::method_writer::write_mul(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::ari_mul, lhs, rhs);
    }
    void generator::method_writer::write_div(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::ari_div, lhs, rhs);
    }
    void generator::method_writer::write_mod(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::ari_mod, lhs, rhs);
    }
    void generator::method_writer::write_add(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::ari_add, lhs, rhs);
    }
    void generator::method_writer::write_sub(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::ari_sub, lhs, rhs);
    }
    void generator::method_writer::write_lsh(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::ari_lsh, lhs, rhs);
    }
    void generator::method_writer::write_rsh(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::ari_rsh, lhs, rhs);
    }
    void generator::method_writer::write_and(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::ari_and, lhs, rhs);
    }
    void generator::method_writer::write_xor(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::ari_xor, lhs, rhs);
    }
    void generator::method_writer::write_or(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::ari_or, lhs, rhs);
    }

    void generator::method_writer::write_padd(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::padd, lhs, rhs);
    }
    void generator::method_writer::write_psub(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::psub, lhs, rhs);
    }
    void generator::method_writer::write_pdif(address lhs, address rhs)
    {
        self().write_expression(opcode::pdif, lhs, rhs);
    }

    void generator::method_writer::write_cmp(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::cmp, lhs, rhs);
    }
    void generator::method_writer::write_ceq(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::ceq, lhs, rhs);
    }
    void generator::method_writer::write_cne(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::cne, lhs, rhs);
    }
    void generator::method_writer::write_cgt(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::cgt, lhs, rhs);
    }
    void generator::method_writer::write_cge(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::cge, lhs, rhs);
    }
    void generator::method_writer::write_clt(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::clt, lhs, rhs);
    }
    void generator::method_writer::write_cle(address lhs, address rhs)
    {
        self().write_sub_expression(opcode::cle, lhs, rhs);
    }
    void generator::method_writer::write_cze(address addr)
    {
        self().write_sub_expression(opcode::cze, addr);
    }
    void generator::method_writer::write_cnz(address addr)
    {
        self().write_sub_expression(opcode::cnz, addr);
    }

    void generator::method_writer::write_br(label_idx label)
    {
        self().write_branch(opcode::br, label);
    }
    void generator::method_writer::write_beq(label_idx label, address lhs, address rhs)
    {
        self().write_branch(opcode::beq, label, lhs, rhs);
    }
    void generator::method_writer::write_bne(label_idx label, address lhs, address rhs)
    {
        self().write_branch(opcode::bne, label, lhs, rhs);
    }
    void generator::method_writer::write_bgt(label_idx label, address lhs, address rhs)
    {
        self().write_branch(opcode::bgt, label, lhs, rhs);
    }
    void generator::method_writer::write_bge(label_idx label, address lhs, address rhs)
    {
        self().write_branch(opcode::bge, label, lhs, rhs);
    }
    void generator::method_writer::write_blt(label_idx label, address lhs, address rhs)
    {
        self().write_branch(opcode::blt, label, lhs, rhs);
    }
    void generator::method_writer::write_ble(label_idx label, address lhs, address rhs)
    {
        self().write_branch(opcode::ble, label, lhs, rhs);
    }
    void generator::method_writer::write_bze(label_idx label, address lhs)
    {
        self().write_branch(opcode::bze, label, lhs);
    }
    void generator::method_writer::write_bnz(label_idx label, address lhs)
    {
        self().write_branch(opcode::bnz, label, lhs);
    }

    void generator::method_writer::write_sw(address addr, span<const label_idx> switch_labels)
    {
        self().write_sw(addr, switch_labels);
    }

    void generator::method_writer::write_call(method_idx method, span<const address> args)
    {
        self().write_call(method, args);
    }
    void generator::method_writer::write_callv(address addr, span<const address> args)
    {
        self().write_callv(addr, args);
    }
    void generator::method_writer::write_ret()
    {
        self().write_ret();
    }
    void generator::method_writer::write_retv(address addr)
    {
        self().write_retv(addr);
    }

    void generator::method_writer::write_dump(address addr)
    {
        self().write_dump(addr);
    }

    void generator::method_writer::finalize()
    {
        auto& writer = self();
        auto& gen = writer.gen;

        ASSERT(writer.bytecode.size() <= static_cast<size_t>(~uint32_t(0)), "Method bytecode exceeds maximum supported value");

        // Ensure the method has returned a value
        if (writer.expects_return_value)
        {
            VALIDATE_HAS_RETURNED(!writer.bytecode.empty() && writer.last_return == writer.bytecode.size(), gen.database[writer.name].name, gen.make_meta(writer.index));
        }

        writer.resolve_labels();

        gen.methods[writer.index] = std::move(writer);
        gen.method_writers[writer.index] = nullptr;

        delete this;
    }


    // Generator
    generator_impl::generator_impl()
    {
        initialize_base_types();

        keybuf.reserve(32);
    }
    generator_impl::~generator_impl()
    {
        for (auto& it : type_writers)
        {
            if (it) delete it;
        }
        for (auto& it : method_writers)
        {
            if (it) delete it;
        }
    }

    generator::generator()
    {

    }
    generator::generator(string_view name) : generator()
    {
        auto& gen = self();
        gen.meta_index = gen.metatable.emplace(name);
    }
    generator::~generator()
    {

    }

    name_idx generator::make_identifier(string_view name)
    {
        VALIDATE_IDENTIFIER(name);

        return self().emplace_identifier(name);
    }

    signature_idx generator::make_signature(type_idx return_type, span<const type_idx> parameter_types)
    {
        auto& gen = self();

        VALIDATE_INDEX(return_type, gen.types.size());
        VALIDATE_TYPES(parameter_types, gen.types.size());
        VALIDATE_PARAM_COUNT(parameter_types.size());

        make_key(return_type, parameter_types, gen.keybuf);
        auto find = gen.signature_lookup.find(gen.keybuf);
        if (find == gen.signature_lookup.end())
        {
            const signature_idx index = signature_idx(gen.signatures.size());

            gen_signature sig;
            sig.index = index;
            sig.return_type = return_type;
            sig.parameters.reserve(parameter_types.size());
            for (auto it : parameter_types)
            {
                sig.parameters.push_back(stackvar(it));
            }

            gen.signature_lookup.emplace(gen.keybuf, index);
            gen.signatures.push_back(std::move(sig));
            return index;
        }
        else
        {
            return find->second;
        }
    }

    offset_idx generator::make_offset(type_idx type, span<const name_idx> fields)
    {
        auto& gen = self();

        VALIDATE_INDEX(type, gen.types.size());
        VALIDATE_INDICES(fields, gen.database.size());
        VALIDATE_OFFSET_VALUE(!fields.empty());

        make_key(type, fields, gen.keybuf);
        auto find = gen.offset_lookup.find(gen.keybuf);
        if (find == gen.offset_lookup.end())
        {
            // New offset
            block<name_idx> field_indices(fields.data(), fields.size());
            gen_field_address addr(type, std::move(field_indices));
            const offset_idx index = offset_idx(gen.offsets.size());
            gen.offsets.push_back(std::move(addr));
            gen.offset_lookup.emplace(gen.keybuf, index);
            return index;
        }
        else
        {
            return find->second;
        }
    }

    offset_idx generator::append_offset(offset_idx offset, span<const name_idx> fields)
    {
        auto& gen = self();

        VALIDATE_INDEX(offset, gen.offsets.size());
        VALIDATE_INDICES(fields, gen.database.size());
        VALIDATE_OFFSET_VALUE(!fields.empty());

        const auto& base = gen.offsets[offset];

        gen.keybuf.clear();
        append_key(gen.keybuf, base.name.object_type);
        for (size_t i = 0; i < base.name.field_names.size(); i++)
        {
            append_key(gen.keybuf, base.name.field_names[i]);
        }
        for (size_t i = 0; i < fields.size(); i++)
        {
            append_key(gen.keybuf, fields[i]);
        }

        auto find = gen.offset_lookup.find(gen.keybuf);
        if (find == gen.offset_lookup.end())
        {
            block<name_idx> field_indices(base.name.field_names.size() + fields.size());
            name_idx* dst = field_indices.data();
            for (size_t i = 0; i < base.name.field_names.size(); i++)
                *dst++ = base.name.field_names[i];
            for (size_t i = 0; i < fields.size(); i++)
                *dst++ = fields[i];
            gen_field_address addr(base.name.object_type, std::move(field_indices));
            const offset_idx index = offset_idx(gen.offsets.size());
            gen.offsets.push_back(std::move(addr));
            gen.offset_lookup.emplace(gen.keybuf, index);
            return index;
        }
        else
        {
            return find->second;
        }
    }

    void generator::define_global(name_idx name, bool is_constant, type_idx type, span<const constant> values)
    {
        self().define_data(is_constant ? lookup_type::constant : lookup_type::global, type, name, values);
    }

    type_idx generator::declare_type(name_idx name)
    {
        auto& gen = self();

        VALIDATE_INDEX(name, gen.database.size());

        auto find = gen.database[name];

        if (find->lookup == lookup_type::identifier)
        {
            // New type
            const type_idx index = type_idx(gen.types.size());
            *find = index;
            gen.types.push_back(gen_type(name, index));
            gen.type_writers.resize(gen.types.size());
            return index;
        }
        else
        {
            VALIDATE_IDENTIFIER_TYPE(find->lookup == lookup_type::type, lookup_type::type, find->lookup, gen.database[name].name);

            // Type already exists
            return find->type;
        }
    }
    generator::type_writer& generator::define_type(type_idx type, bool is_union)
    {
        auto& gen = self();

        VALIDATE_INDEX(type, gen.types.size());

        auto& dst = gen.types[type];
        generator::type_writer*& writer = gen.type_writers[type];
        VALIDATE_TYPE_DEC(!dst.is_defined() && !writer, gen.database[dst.name].name, gen.make_meta(dst.index));

        dst.meta.index = gen.meta_index;
        dst.meta.line_number = gen.line_number;

        writer = new generator::type_writer(gen, dst.name, dst.index, is_union);
        return *writer;
    }

    type_idx generator::declare_pointer_type(type_idx base_type)
    {
        auto& gen = self();

        VALIDATE_INDEX(base_type, gen.types.size());

        auto& type = gen.types[base_type];
        if (type.pointer_type == type_idx::invalid)
        {
            // New pointer type
            const type_idx idx = type_idx(gen.types.size());
            gen_type generate_type(name_idx::invalid, idx);
            generate_type.make_pointer(base_type);
            generate_type.flags |= extended_flags::is_defined;
            type.pointer_type = idx;
            gen.types.push_back(std::move(generate_type));
            return idx;
        }

        // Existing pointer type
        return type.pointer_type;
    }
    type_idx generator::declare_array_type(type_idx base_type, size_t array_size)
    {
        auto& gen = self();

        VALIDATE_INDEX(base_type, gen.types.size());
        VALIDATE_ARRAY_LENGTH(array_size);

        auto& type = gen.types[base_type];
        auto find = type.array_types.find(array_size);
        if (find == type.array_types.end())
        {
            // New array type
            const type_idx idx = type_idx(gen.types.size());
            gen_type generate_type(name_idx::invalid, idx);
            generate_type.make_array(base_type, array_size);
            generate_type.flags |= extended_flags::is_defined;
            type.array_types.emplace(array_size, idx);
            gen.types.push_back(std::move(generate_type));
            return idx;
        }

        // Existing array type
        return find->second;
    }
    type_idx generator::declare_signature_type(signature_idx signature)
    {
        auto& gen = self();

        VALIDATE_INDEX(signature, gen.signatures.size());

        gen_signature& sig = gen.signatures[signature];
        if (sig.signature_type == type_idx::invalid)
        {
            // New signature type
            const type_idx idx = type_idx(gen.types.size());
            gen_type generate_type(name_idx::invalid, idx);
            generate_type.make_signature(signature);
            generate_type.flags |= extended_flags::is_defined;
            sig.signature_type = idx;
            gen.types.push_back(std::move(generate_type));
            return idx;
        }

        // Existing signature type
        return sig.signature_type;
    }

    method_idx generator::declare_method(name_idx name)
    {
        auto& gen = self();

        VALIDATE_INDEX(name, gen.database.size());

        auto find = gen.database[name];

        if (find->lookup == lookup_type::identifier)
        {
            // New method
            const method_idx index = method_idx(gen.methods.size());
            *find = index;
            gen.methods.push_back(gen_method(name, index));
            gen.method_writers.resize(gen.methods.size());
            return index;
        }
        else
        {
            VALIDATE_IDENTIFIER_TYPE(find->lookup == lookup_type::method, lookup_type::method, find->lookup, gen.database[name].name);

            // Existing method
            return find->method;
        }
    }
    generator::method_writer& generator::define_method(method_idx method, signature_idx signature)
    {
        auto& gen = self();

        VALIDATE_INDEX(method, gen.methods.size());
        VALIDATE_INDEX(signature, gen.signatures.size());

        auto& dst = gen.methods[method];
        generator::method_writer*& writer = gen.method_writers[method];
        VALIDATE_METHOD_DEC(!dst.is_defined() && !writer, gen.database[dst.name].name, gen.make_meta(dst.index));

        dst.meta.index = gen.meta_index;
        dst.meta.line_number = gen.line_number;

        writer = new generator::method_writer(gen, dst.name, dst.index, signature);
        return *writer;
    }

    void generator::set_line_number(uint32_t line_number) noexcept
    {
        self().line_number = line_number;
    }

    intermediate generator::finalize()
    {
        auto& gen = self();

        // Finalize can throw
        // Finalize cleans up the writer and sets itself null in the array
        for (auto& it : gen.type_writers)
        {
            if (it) it->finalize();
        }
        for (auto& it : gen.method_writers)
        {
            if (it) it->finalize();
        }

        intermediate result;
        gen_intermediate_data::serialize(result, gen);
        return result;
    }


    file_meta generator::type_writer::get_meta() const
    {
        return self().gen.get_meta();
    }
    file_meta generator::method_writer::get_meta() const
    {
        return self().gen.get_meta();
    }
    file_meta generator::get_meta() const
    {
        return self().get_meta();
    }
}