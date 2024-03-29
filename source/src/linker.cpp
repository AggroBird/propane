#include "assembly_data.hpp"
#include "operations.hpp"
#include "errors.hpp"
#include "constants.hpp"
#include "utility.hpp"
#include "name_generator.hpp"
#include "library.hpp"

#define VALIDATE(errc, expr, ...) ENSURE(errc, expr, propane::linker_exception, __VA_ARGS__)

#define VALIDATE_INTERMEDIATE(expr) VALIDATE(ERRC::LNK_INVALID_INTERMEDIATE, expr, \
    "Attempted to link an invalid intermediate")
#define VALIDATE_COMPATIBILITY(expr) VALIDATE(ERRC::LNK_INCOMPATIBLE_INTERMEDIATE, expr, \
    "Attempted to link an intermediate that was build using an incompatible toolchain")
#define VALIDATE_TYPE_RECURSIVE(expr, name) VALIDATE(ERRC::LNK_RECURSIVE_TYPE_DEFINITION, expr, \
    "Type definition for '%' is recursive", name)
#define VALIDATE_TYPE_DEFINITION(expr, name) VALIDATE(ERRC::LNK_UNDEFINED_TYPE, expr, \
    "Failed to find a definition for type '%'", name)
#define VALIDATE_METHOD_DEFINITION(expr, name) VALIDATE(ERRC::LNK_UNDEFINED_METHOD, expr, \
    "Failed to find a definition for method '%'", name)
#define VALIDATE_GLOBAL_DEFINITION(expr, name) VALIDATE(ERRC::LNK_UNDEFINED_GLOBAL, expr, \
    "Failed to find a definition for global '%'", name)
#define VALIDATE_TYPE_SIZE(expr, name, type_meta) VALIDATE(ERRC::LNK_TYPE_SIZE_ZERO, expr, \
    "Size of type '%' (%) evaluated to zero", name, type_meta)
#define VALIDATE_METHOD_PTR_INITIALIZER(expr, name) VALIDATE(ERRC::LNK_UNINITIALIZED_METHOD_PTR, expr, \
    "Method pointer constant requires initialization (initialization of global '%')", name)
#define VALIDATE_METHOD_INITIALIZER_DEFINITION(expr, name, method_name) VALIDATE(ERRC::LNK_UNDEFINED_METHOD_INITIALIZER, expr, \
    "Failed to find a definition for method '%' (initialization of global '%')",method_name, name)
#define VALIDATE_METHOD_INITIALIZER(expr, name) VALIDATE(ERRC::LNK_INVALID_METHOD_INITIALIZER, expr, \
    "Invalid type provided for method pointer initialization (initialization of global '%')", name)
#define VALIDATE_GLOBAL_INITIALIZER_COUNT(expr, provided, required, name) VALIDATE(ERRC::LNK_GLOBAL_INITIALIZER_OVERFLOW, expr, \
    "Too many initializer values provided for global: % provided where a maximum of % is expected (initialization of global '%')", provided, required, name)
#define VALIDATE_TYPE_FIELD_DEFINITION(expr, field_name, type, type_meta) VALIDATE(ERRC::LNK_UNDEFINED_TYPE_FIELD, expr, \
    "Failed to find field '%' (see definition of type '%' at '%')", field_name, type, type_meta)

// This only works in the assembly_linker class
#define VALIDATE_INSTRUCTION(errc, expr, fmt, ...) VALIDATE(ERRC::LNK_INVALID_IMPLICIT_CONVERSION, expr, \
    fmt " (See definition of method '%' at '%', instruction #%: %)", __VA_ARGS__ \
    this->database[this->current_method->name].name, this->make_meta(this->current_method->index), this->iidx, propane::opcode_str(this->current_op))
#define VALIDATE_INSTRUCTION_NOARG(errc, expr, fmt) VALIDATE(ERRC::LNK_INVALID_IMPLICIT_CONVERSION, expr, \
    fmt " (See definition of method '%' at '%', instruction #%: %)", \
    this->database[this->current_method->name].name, this->make_meta(this->current_method->index), this->iidx, propane::opcode_str(this->current_op))

#define VALIDATE_IMPLICIT_CONVERSION(expr, lhs_type, rhs_type) VALIDATE_INSTRUCTION(ERRC::LNK_INVALID_IMPLICIT_CONVERSION, expr, \
    "Invalid implicit conversion between types '%' and '%'", this->get_name(lhs_type), this->get_name(rhs_type),)
#define VALIDATE_EXPLICIT_CONVERSION(expr, lhs_type, rhs_type) VALIDATE_INSTRUCTION(ERRC::LNK_INVALID_EXPLICIT_CONVERSION, expr, \
    "Invalid explicit conversion between types '%' and '%'", this->get_name(lhs_type), this->get_name(rhs_type),)
#define VALIDATE_ARITHMETIC_EXPRESSION(expr, lhs_type, rhs_type) VALIDATE_INSTRUCTION(ERRC::LNK_INVALID_ARITHMETIC_EXPRESSION, expr, \
    "Invalid arithmetic expression between types '%' and '%'", this->get_name(lhs_type), this->get_name(rhs_type),)
#define VALIDATE_COMPARISON_EXPRESSION(expr, lhs_type, rhs_type) VALIDATE_INSTRUCTION(ERRC::LNK_INVALID_COMPARISON_EXPRESSION, expr, \
    "Invalid comparison expression between types '%' and '%'", this->get_name(lhs_type), this->get_name(rhs_type),)
#define VALIDATE_POINTER_EXPRESSION(expr, lhs_type, rhs_type) VALIDATE_INSTRUCTION(ERRC::LNK_INVALID_POINTER_EXPRESSION, expr, \
    "Invalid pointer expression between types '%' and '%'", this->get_name(lhs_type), this->get_name(rhs_type),)
#define VALIDATE_PTR_OFFSET_EXPRESSION(expr, lhs_type, rhs_type) VALIDATE_INSTRUCTION(ERRC::LNK_INVALID_PTR_OFFSET_EXPRESSION, expr, \
    "Unable to take pointer offset between types '%' and '%'", this->get_name(lhs_type), this->get_name(rhs_type),)
#define VALIDATE_SWITCH_TYPE(expr, type) VALIDATE_INSTRUCTION(ERRC::LNK_INVALID_SWITCH_TYPE, expr, \
    "Non-integral type '%' is not valid for switch instruction", this->get_name(type),)
#define VALIDATE_ARGUMENT_COUNT(expr, provided, expected) VALIDATE_INSTRUCTION(ERRC::LNK_FUNCTION_ARGUMENT_COUNT_MISMATCH, expr, \
    "Provided argument count does not match signature parameter count: % provided where % was expected", provided, expected,)
#define VALIDATE_SIGNATURE_TYPE_INVOCATION(expr, type) VALIDATE_INSTRUCTION(ERRC::LNK_NON_SIGNATURE_TYPE_INVOKE, expr, \
    "Type '%' is not a valid method pointer", this->get_name(type),)
#define VALIDATE_RETURN_ADDRESS(expr) VALIDATE_INSTRUCTION_NOARG(ERRC::LNK_INVALID_RETURN_ADDRESS, expr, \
    "Return value address is not valid here")
#define VALIDATE_ARRAY_INDEX(expr, index, array_type_name) VALIDATE_INSTRUCTION(ERRC::LNK_ARRAY_INDEX_OUT_OF_RANGE, expr, \
    "Constant array index out of range (Index % in array %)", index, array_type_name,)
#define VALIDATE_OFFSET_MODIFIER(expr, type_name) VALIDATE_INSTRUCTION(ERRC::LNK_INVALID_OFFSET_MODIFIER, expr, \
    "Unable to apply offset modifier on type '%'", type_name,)
#define VALIDATE_FIELD_PARENT_TYPE(expr, parent_type_name, provided_type_name) VALIDATE_INSTRUCTION(ERRC::LNK_FIELD_PARENT_TYPE_MISMATCH, expr, \
    "Field offset root type '%' does not match variable type '%'", parent_type_name, provided_type_name,)
#define VALIDATE_POINTER_DEREFERENCE(expr, type_name) VALIDATE_INSTRUCTION(ERRC::LNK_INVALID_POINTER_DEREFERENCE, expr, \
    "Unable to dereference non-pointer type '%'", type_name,)
#define VALIDATE_VOIDPOINTER_DEREFERENCE(expr, type_name) VALIDATE_INSTRUCTION(ERRC::LNK_ABSTRACT_POINTER_DEREFERENCE, expr, \
    "Unable to dereference abstract pointer type '%'", type_name,)
#define VALIDATE_FIELD_DEREFERENCE(expr, type_name) VALIDATE_INSTRUCTION(ERRC::LNK_INVALID_FIELD_DEREFERENCE, expr, \
    "Unable to dereference field on type '%'", type_name,)


namespace propane
{
    // Assembly linker takes in an intermediate that has been merged
    // and links up the references. It recompiles the bytecode and replaces
    // the lookup indices with the actual type/method indices
    class assembly_linker final : public asm_assembly_data
    {
    public:
        assembly_linker(gen_intermediate_data&& im_data, const runtime& runtime) :
            data(std::move(im_data)),
            size_type(derive_type_index_v<size_t>),
            offset_type(derive_type_index_v<offset_t>),
            ptr_size(get_base_type_size(type_idx::vptr))
        {
            data.restore_generated_types();

            // Setup runtime
            auto& rt_data = runtime.self();
            if (!rt_data.call_lookup.empty())
            {
                keybuf.reserve(32);

                for (auto& it : data.methods)
                {
                    if (it.is_defined()) continue;

                    const string_view method_name = data.database[it.name].name;
                    auto find_external = rt_data.call_lookup.find(method_name);
                    VALIDATE_METHOD_DEFINITION(find_external != rt_data.call_lookup.end(), method_name);

                    // Create signature
                    auto cidx = find_external->second;
                    const external_call_info& call = rt_data.libraries[cidx.library].calls[cidx.index];
                    const signature_idx sig_idx = resolve_native_types(call);

                    // Create method
                    gen_method method(it.name, it.index);
                    method.signature = sig_idx;
                    append_bytecode(method.bytecode, cidx);
                    method.flags |= (extended_flags::is_defined | type_flags::is_external);
                    it = std::move(method);
                }
            }

            // Resolve any missing types
            vector<string_view> unresolved_external_types;
            for (auto& it : data.types)
            {
                if (it.is_defined()) continue;

                unresolved_external_types.push_back(data.database[it.name].name);
            }
            for (auto& it : unresolved_external_types)
            {
                auto find_external = rt_data.type_lookup.find(it);
                VALIDATE_TYPE_DEFINITION(find_external != rt_data.type_lookup.end(), it);

                resolve_native_type(find_external->second);
            }

            // Set hash
            runtime_hash = rt_data.hash;

            // Move over objects
            for (auto& t : data.types) types.push_back(std::move(t));
            for (auto& m : data.methods) methods.push_back(std::move(m));
            for (auto& s : data.signatures) signatures.push_back(std::move(s));
            for (auto& o : data.offsets) offsets.push_back(std::move(o));

            // Move over data
            globals = std::move(data.globals);
            constants = std::move(data.constants);
            database = std::move(data.database);
            metatable = std::move(data.metatable);

            // Resolve types, methods and signatures
            for (auto& t : types) if (!t.is_resolved()) resolve_type_recursive(t);
            for (auto& s : signatures) if (!s.is_resolved) resolve_signature(s);
            // Resolve offsets
            resolve_offsets();
            // Resolve methods (after everything else)
            for (auto& m : methods) if (!m.is_resolved()) resolve_method(m);

            // Link constants
            initialize_data_table(constants, true);
            initialize_data_table(globals, false);

            // Find main
            find_main();
        }

        vector<uint8_t> keybuf;
        vector<stackvar> params;

        signature_idx resolve_native_types(const external_call_info& call)
        {
            const type_idx return_type = resolve_native_type(call.return_type);
            params.resize(call.parameters.size());
            stackvar* dst = params.data();
            for (auto& p : call.parameters)
            {
                *dst++ = stackvar(resolve_native_type(p), p.offset);
            }

            make_key<stackvar>(return_type, params, keybuf);
            auto find = data.signature_lookup.find(keybuf);
            if (find == data.signature_lookup.end())
            {
                // New signature
                const signature_idx sig_idx = signature_idx(data.signatures.size());
                gen_signature signature(sig_idx, return_type, std::move(params));
                signature.is_resolved = true;
                signature.parameters_size = call.parameters_size;
                data.signature_lookup.emplace(keybuf, sig_idx);
                data.signatures.push_back(std::move(signature));
                return sig_idx;
            }
            else
            {
                return find->second;
            }
        }
        type_idx resolve_native_type(const native::typedecl& native_type)
        {
            type_idx result_idx;

            if (auto find = data.database.find(native_type.name))
            {
                // Existing type
                ASSERT(find->lookup == lookup_type::type, "Invalid type");

                result_idx = find->type;

                if (!is_base_type(find->type))
                {
                    auto& type = data.types[result_idx];

                    ASSERT(type.total_size == 0 || type.total_size == native_type.size, "Native type size mismatch");

                    // Natives are implicitly defined
                    type.total_size = native_type.size;
                    type.flags |= type_flags::is_external;
                    type.flags |= extended_flags::is_defined;
                }
            }
            else
            {
                // New type
                result_idx = type_idx(data.types.size());
                const name_idx name = data.database.emplace(native_type.name, result_idx).key;
                gen_type type(name, result_idx);
                type.total_size = native_type.size;
                type.flags |= type_flags::is_external;
                type.flags |= extended_flags::is_defined;
                data.types.push_back(std::move(type));
            }

            // Create field offsets
            {
                auto& type = data.types[result_idx];
                for (auto& field : native_type.fields)
                {
                    if (auto find_field_type = data.database.find(field.type))
                    {
                        ASSERT(find_field_type->lookup == lookup_type::type, "NYI");

                        const auto find_field_name = data.database.find(field.name);
                        const name_idx field_name = find_field_name ? find_field_name.key : data.database.emplace(field.name, lookup_idx::make_identifier()).key;
                        type.fields.push_back(propane::field(field_name, find_field_type->type));
                    }
                    else
                    {
                        VALIDATE_TYPE_DEFINITION(false, field.type);
                    }
                }
            }

            // Resolve pointers
            for (size_t i = 0; i < native_type.pointer_depth; i++)
            {
                type_idx idx = data.types[result_idx].pointer_type;
                if (idx == type_idx::invalid)
                {
                    // Create a new pointer for this type
                    idx = type_idx(data.types.size());
                    gen_type generate_type(name_idx::invalid, idx);
                    generate_type.make_pointer(result_idx);
                    generate_type.flags |= extended_flags::is_defined;
                    data.types[result_idx].pointer_type = idx;
                    data.types.push_back(std::move(generate_type));
                }
                result_idx = idx;
            }

            return result_idx;
        }

        void resolve_type_recursive(asm_type& type)
        {
            if (!type.is_resolved())
            {
                VALIDATE_TYPE_RECURSIVE(!(type.flags & extended_flags::is_resolving), get_name(type));
                type.flags |= extended_flags::is_resolving;

                VALIDATE_TYPE_DEFINITION(type.is_defined(), get_name(type));

                if (is_base_type(type.index))
                {
                    // Base type (build-in)
                    type.total_size = get_base_type_size(type.index);
                    type.flags |= extended_flags::is_resolved;
                }
                else if (type.is_generated())
                {
                    if (type.is_pointer())
                    {
                        // Pointer
                        type.total_size = ptr_size;
                    }
                    else if (type.is_array())
                    {
                        // Array
                        auto& underlying_type = types[type.generated.array.underlying_type];
                        resolve_type_recursive(underlying_type);
                        type.total_size = underlying_type.total_size * type.generated.array.array_size;
                    }
                    else if (type.is_signature())
                    {
                        // Signature
                        type.total_size = ptr_size;
                    }
                    else
                    {
                        ASSERT(false, "Malformed type flag");
                    }

                    type.flags |= extended_flags::is_resolved;
                }
                else
                {
                    // User-defined types
                    if (!type.fields.empty())
                    {
                        const auto current_size = type.total_size;
                        type.total_size = 0;
                        for (auto& field : type.fields)
                        {
                            auto& field_type = types[field.type];
                            resolve_type_recursive(field_type);
                            field.offset = type.is_union() ? 0 : type.total_size;
                            type.total_size = type.is_union() ? std::max(type.total_size, field_type.total_size) : (type.total_size + field_type.total_size);
                        }
                        // Ensure that size matches native declaration
                        ASSERT(current_size == 0 || current_size == type.total_size, "Native type size mismatch");
                    }
                    VALIDATE_TYPE_SIZE(type.total_size > 0, get_name(type), make_meta(type.index));
                    type.flags |= extended_flags::is_resolved;
                }
            }

            // Pointer types underlying size needs to be resolved after the underlying type
            // knows its own size
            if (type.pointer_type != type_idx::invalid)
            {
                auto& pointer_type = types[type.pointer_type];
                pointer_type.generated.pointer.underlying_size = type.total_size;
            }
        }

        void resolve_method(asm_method& method)
        {
            VALIDATE_METHOD_DEFINITION(method.is_defined(), get_name(method));

            // Translate global indices
            for (auto& g : method.globals)
            {
                const name_idx name = g.name;
                auto find = database[name];
                if (find->lookup == lookup_type::method)
                {
                    // Method addresses are generated on demand, we dont have to generate one for every method
                    g.index = resolve_method_constant(methods[find->method]);
                }
                else
                {
                    VALIDATE_GLOBAL_DEFINITION(find->lookup == lookup_type::constant || find->lookup == lookup_type::global, find.name);
                    g.index = global_idx(find->index);
                    if (find->lookup == lookup_type::constant) g.index |= global_flags::constant_flag;
                }
            }

            // Stack variables
            asm_signature& method_signature = signatures[method.signature];
            {
                method.method_stack_size = method_signature.parameters_size;
                size_t variable_stack_size = 0;
                for (auto& sv : method.stackvars)
                {
                    sv.offset = variable_stack_size;
                    variable_stack_size += types[sv.type].total_size;
                }
                method.method_stack_size += variable_stack_size;
            }

            // Recompile
            max_return_value_size = 0;
            if (!method.is_external())
            {
                current_method = &method;
                current_signature = &method_signature;
                return_value = type_idx::voidtype;

                if (!method.bytecode.empty())
                {
                    labels = method.labels;
                    label_idx = 0;

                    uint8_t* const ibeg = method.bytecode.data();
                    uint8_t* const iend = ibeg + method.bytecode.size();
                    iptr = ibeg;
                    iidx = 0;
                    bool has_returned = false;
                    while (true)
                    {
                        ASSERT(iptr >= ibeg && iptr <= iend, "Instruction pointer out of range");

                        const uint32_t offset = static_cast<uint32_t>(iptr - ibeg);
                        while (label_idx < labels.size() && offset >= labels[label_idx])
                        {
                            // Ensure labels are at the correct location
                            ASSERT(offset == labels[label_idx], "Invalid label offset");
                            label_idx++;
                            clear_return_value();
                        }

                        if (iptr == iend)
                        {
                            if (!has_returned)
                            {
                                // Make sure that the method returns a value if expected
                                ASSERT(!current_signature->has_return_value(), "Function expects a return value");

                                // If method bytecode ends without a return, append one
                                append_bytecode(current_method->bytecode, opcode::ret);
                            }

                            break;
                        }

                        has_returned = false;

                        iidx++;

                        current_op = read_bytecode<opcode>(iptr);
                        switch (current_op)
                        {
                            case opcode::noop:
                                break;

                            case opcode::set:
                            {
                                subcode& sub = read_subcode();
                                const type_idx lhs = resolve_address();
                                const type_idx rhs = resolve_operand(lhs);
                                sub = resolve_set(lhs, rhs);
                            }
                            break;

                            case opcode::conv:
                            {
                                subcode& sub = read_subcode();
                                const type_idx lhs = resolve_address();
                                const type_idx rhs = resolve_operand(lhs);
                                sub = resolve_conv(lhs, rhs);
                            }
                            break;

                            case opcode::ari_not:
                            case opcode::ari_neg:
                            {
                                subcode& sub = read_subcode();
                                const type_idx lhs = resolve_address();
                                sub = resolve_ari(current_op, lhs, lhs);
                            }
                            break;

                            case opcode::ari_mul:
                            case opcode::ari_div:
                            case opcode::ari_mod:
                            case opcode::ari_add:
                            case opcode::ari_sub:
                            case opcode::ari_lsh:
                            case opcode::ari_rsh:
                            case opcode::ari_and:
                            case opcode::ari_xor:
                            case opcode::ari_or:
                            {
                                subcode& sub = read_subcode();
                                const type_idx lhs = resolve_address();
                                const type_idx rhs = resolve_operand(lhs);
                                sub = resolve_ari(current_op, lhs, rhs);
                            }
                            break;

                            case opcode::padd:
                            case opcode::psub:
                            {
                                subcode& sub = read_subcode();
                                const type_idx lhs = resolve_address();
                                const type_idx rhs = resolve_operand(lhs);
                                sub = resolve_ptr(current_op, lhs, rhs);
                            }
                            break;

                            case opcode::pdif:
                            {
                                const type_idx lhs = resolve_address();
                                const type_idx rhs = resolve_operand(lhs);
                                resolve_pdif(lhs, rhs);
                                // Pointer dif return value
                                set_return_value(offset_type);
                            }
                            break;

                            case opcode::cmp:
                            case opcode::ceq:
                            case opcode::cne:
                            case opcode::cgt:
                            case opcode::cge:
                            case opcode::clt:
                            case opcode::cle:
                            {
                                subcode& sub = read_subcode();
                                const type_idx lhs = resolve_address();
                                const type_idx rhs = resolve_operand(lhs);
                                sub = resolve_cmp(current_op, lhs, rhs);
                                // Comparison return value
                                set_return_value(type_idx::i32);
                            }
                            break;

                            case opcode::cze:
                            case opcode::cnz:
                            {
                                subcode& sub = read_subcode();
                                const type_idx lhs = resolve_operand();
                                sub = resolve_cmp(current_op, lhs, lhs);
                                // Comparison return value
                                set_return_value(type_idx::i32);
                            }
                            break;

                            case opcode::br:
                            {
                                const uint32_t jump = read_bytecode<uint32_t>(iptr);
                                // Reset return value after branch
                                clear_return_value();
                            }
                            break;

                            case opcode::beq:
                            case opcode::bne:
                            case opcode::bgt:
                            case opcode::bge:
                            case opcode::blt:
                            case opcode::ble:
                            {
                                const uint32_t jump = read_bytecode<uint32_t>(iptr);
                                subcode& sub = read_subcode();
                                const type_idx lhs = resolve_address();
                                const type_idx rhs = resolve_operand(lhs);
                                sub = resolve_cmp(current_op - (opcode::br - opcode::cmp), lhs, rhs);
                                // Reset return value after branch
                                clear_return_value();
                            }
                            break;

                            case opcode::bze:
                            case opcode::bnz:
                            {
                                const uint32_t jump = read_bytecode<uint32_t>(iptr);
                                subcode& sub = read_subcode();
                                const type_idx lhs = resolve_operand();
                                sub = resolve_cmp(current_op - (opcode::br - opcode::cmp), lhs, lhs);
                                // Reset return value after branch
                                clear_return_value();
                            }
                            break;

                            case opcode::sw:
                            {
                                const type_idx type = resolve_operand();
                                VALIDATE_SWITCH_TYPE(is_integral(type), type);
                                const uint32_t label_count = read_bytecode<uint32_t>(iptr);
                                iptr += sizeof(uint32_t) * label_count;
                                // Reset return value after branch
                                clear_return_value();
                            }
                            break;

                            case opcode::call:
                            {
                                // Translate method index
                                uint32_t& idx = read_bytecode_ref<uint32_t>(iptr);
                                idx = (uint32_t)method.calls[idx];
                                const size_t arg_count = static_cast<size_t>(read_bytecode<uint8_t>(iptr));
                                const auto& call_method = methods[method_idx(idx)];
                                VALIDATE_METHOD_DEFINITION(call_method.is_defined(), get_name(call_method));
                                const auto& signature = signatures[call_method.signature];
                                VALIDATE_ARGUMENT_COUNT(arg_count == signature.parameters.size(), arg_count, signature.parameters.size());
                                for (size_t i = 0; i < arg_count; i++)
                                {
                                    subcode& sub = read_subcode();
                                    const type_idx arg_type = resolve_operand(signature.parameters[i].type);
                                    sub = resolve_set(signature.parameters[i].type, arg_type);
                                }
                                // Set return value to method return type
                                set_return_value(signature.return_type);
                            }
                            break;

                            case opcode::callv:
                            {
                                const type_idx type = resolve_operand();
                                VALIDATE_SIGNATURE_TYPE_INVOCATION(types[type].is_signature(), type);
                                const size_t arg_count = static_cast<size_t>(read_bytecode<uint8_t>(iptr));
                                const auto& signature = signatures[types[type].generated.signature.index];
                                VALIDATE_ARGUMENT_COUNT(arg_count == signature.parameters.size(), arg_count, signature.parameters.size());
                                for (size_t i = 0; i < arg_count; i++)
                                {
                                    subcode& sub = read_subcode();
                                    const type_idx arg_type = resolve_operand(signature.parameters[i].type);
                                    sub = resolve_set(signature.parameters[i].type, arg_type);
                                }
                                // Set return value to method return type
                                set_return_value(signature.return_type);
                            }
                            break;

                            case opcode::ret:
                            {
                                ASSERT(!current_signature->has_return_value(), "Function expects a return value");
                                has_returned = true;
                            }
                            break;

                            case opcode::retv:
                            {
                                ASSERT(current_signature->has_return_value(), "Function does not return a value");
                                has_returned = true;

                                subcode& sub = read_subcode();
                                const type_idx rhs = resolve_operand(current_signature->return_type);
                                sub = resolve_set(current_signature->return_type, rhs);
                            }
                            break;

                            case opcode::dump:
                                resolve_operand();
                                break;

                            default: ASSERT(false, "Malformed opcode");
                        }
                    }
                }
                else
                {
                    ASSERT(!current_signature->has_return_value(), "Function expects a return value");

                    append_bytecode(current_method->bytecode, opcode::ret);
                }
            }
            method.total_stack_size = method.method_stack_size + max_return_value_size;

            // Clear lookup
            method.calls.clear();
            method.offsets.clear();
            method.globals.clear();

            method.flags |= extended_flags::is_resolved;
        }

        subcode& read_subcode()
        {
            return read_bytecode_ref<subcode>(iptr);
        }
        type_idx resolve_address()
        {
            return resolve_address_type(type_idx::invalid);
        }
        type_idx resolve_operand(type_idx lhs = type_idx::voidtype)
        {
            return resolve_address_type(lhs);
        }
        type_idx resolve_address_type(type_idx lhs)
        {
            // lhs type of invalid indicates that this address is left-hand-side
            // lhs type of voidtype indicates that this address is right-hand-side, but no expected type
            // lhs type of any other type indicates that special casting rules can apply

            type_idx last_type = type_idx::invalid;

            const auto& minf = *current_method;
            const auto& csig = *current_signature;

            address_data_t& addr = *reinterpret_cast<address_data_t*>(iptr);

            const uint32_t index = addr.header.index();
            switch (addr.header.type())
            {
                case address_type::stackvar:
                {
                    if (index == address_header_constants::index_max)
                    {
                        VALIDATE_RETURN_ADDRESS(return_value != type_idx::voidtype);

                        last_type = return_value;
                    }
                    else
                    {
                        ASSERT(index < minf.stackvars.size(), "Stack index out of range");

                        last_type = minf.stackvars[index].type;
                    }
                }
                break;

                case address_type::parameter:
                {
                    ASSERT(index < csig.parameters.size(), "Parameter index out of range");

                    last_type = csig.parameters[index].type;
                }
                break;

                case address_type::global:
                {
                    // Translate global index
                    addr.header.set_index((uint32_t)minf.globals[index].index);

                    global_idx global = (global_idx)addr.header.index();

                    const bool is_constant = is_constant_flag_set(global);
                    const auto& table = is_constant ? constants : globals;
                    global &= global_flags::constant_mask;

                    ASSERT(table.info.is_valid_index(global), "Parameter index out of range");

                    last_type = table.info[global].type;
                }
                break;

                case address_type::constant:
                {
                    const type_idx btype_idx = type_idx(index);

                    // All of these cases should have been caught by the parser already
                    ASSERT(lhs != type_idx::invalid, "Constant cannot be a left-hand side operand");
                    ASSERT(btype_idx <= type_idx::vptr, "Malformed constant opcode");
                    ASSERT(addr.header.modifier() == address_modifier::none, "Cannot apply address modifier on a constant");
                    ASSERT(addr.header.prefix() == address_prefix::none, "Cannot apply address prefix on a constant");

                    iptr += (types[btype_idx].total_size + sizeof(address_header));

                    // Cast to destination type if assigning null pointer
                    if (types[lhs].is_pointer() && btype_idx == type_idx::vptr)
                    {
                        return lhs;
                    }

                    return btype_idx;
                }
                break;

                default: ASSERT(false, "Malformed address header");
            }

            switch (addr.header.modifier())
            {
                case address_modifier::none: break;

                case address_modifier::direct_field:
                {
                    // Translate field offset
                    addr.field = minf.offsets[static_cast<size_t>(addr.field)];

                    const auto& field = offsets[addr.field];

                    const auto& type = types[last_type];
                    VALIDATE_FIELD_DEREFERENCE(!type.is_pointer(), get_name(type));
                    VALIDATE_FIELD_PARENT_TYPE(type.index == field.name.object_type, get_name(field.name.object_type), get_name(type));

                    last_type = field.type;
                }
                break;

                case address_modifier::indirect_field:
                {
                    // Translate field offset
                    addr.field = minf.offsets[static_cast<size_t>(addr.field)];

                    const auto& field = offsets[addr.field];

                    const auto& type = types[last_type];
                    VALIDATE_POINTER_DEREFERENCE(type.is_pointer(), get_name(type.index));
                    const auto& underlying_type = types[type.generated.pointer.underlying_type];
                    VALIDATE_FIELD_PARENT_TYPE(underlying_type.index == field.name.object_type, get_name(field.name.object_type), get_name(underlying_type));

                    last_type = field.type;
                }
                break;

                case address_modifier::offset:
                {
                    const auto& type = types[last_type];
                    if (type.is_pointer())
                    {
                        last_type = type.generated.pointer.underlying_type;
                    }
                    else if (type.is_array())
                    {
                        last_type = type.generated.array.underlying_type;
                        VALIDATE_ARRAY_INDEX(addr.offset >= 0 && static_cast<size_t>(addr.offset) < type.generated.array.array_size, addr.offset, get_name(type));
                    }
                    else
                    {
                        VALIDATE_OFFSET_MODIFIER(false, get_name(type));
                    }
                }
                break;

                default: ASSERT(false, "Malformed address header");
            }

            switch (addr.header.prefix())
            {
                case address_prefix::none: break;

                case address_prefix::indirection:
                {
                    const auto& type = types[last_type];

                    VALIDATE_POINTER_DEREFERENCE(type.is_pointer(), get_name(type));
                    const auto& underlying_type = types[type.generated.pointer.underlying_type];
                    VALIDATE_VOIDPOINTER_DEREFERENCE(underlying_type.index != type_idx::voidtype, get_name(type));

                    last_type = type.generated.pointer.underlying_type;
                }
                break;

                case address_prefix::address_of:
                {
                    auto& type = types[last_type];

                    if (type.pointer_type == type_idx::invalid)
                    {
                        // Generate missing pointer type
                        // It is possible for generators to skip pointer types that
                        // are never declared as stack variable, parameter, global or field
                        gen_type pointer_type = gen_type(name_idx::invalid, type_idx(types.size()));
                        pointer_type.flags = extended_flags::is_defined | extended_flags::is_resolved;
                        pointer_type.total_size = ptr_size;
                        pointer_type.make_pointer(type.index);
                        type.pointer_type = pointer_type.index;
                        last_type = pointer_type.index;
                        types.push_back(std::move(pointer_type));
                    }
                    else
                    {
                        last_type = type.pointer_type;
                    }
                }
                break;

                case address_prefix::size_of:
                {
                    last_type = size_type;
                }
                break;

                default: ASSERT(false, "Malformed address header");
            }

            iptr += sizeof(address_data_t);

            return last_type;
        }

        void resolve_signature(asm_signature& signature)
        {
            size_t offset = 0;
            for (auto& p : signature.parameters)
            {
                p.offset = offset;
                offset += types[p.type].total_size;
            }
            signature.parameters_size = offset;

            signature.is_resolved = true;
        }

        void resolve_offsets()
        {
            for (auto& f : offsets)
            {
                ASSERT(!f.name.field_names.empty(), "Invalid empty field name array");

                const auto* type = &types[f.name.object_type];
                f.offset = 0;

                // Resolve offset per identifier
                for (auto& fn : f.name.field_names)
                {
                    type_idx field_type = type_idx::invalid;
                    for (const auto& field : type->fields)
                    {
                        if (field.name == fn)
                        {
                            f.offset += field.offset;
                            field_type = field.type;
                            type = &types[field_type];
                            break;
                        }
                    }

                    VALIDATE_TYPE_FIELD_DEFINITION(field_type != type_idx::invalid, get_name(fn), get_name(*type), make_meta(type->index));
                    f.type = field_type;
                }

                VALIDATE_TYPE_FIELD_DEFINITION(f.type != type_idx::invalid, get_name(f.name.field_names[0]), get_name(*type), make_meta(type->index))
            }
        }

        void find_main()
        {
            if (auto find = database.find("main"))
            {
                if (find->lookup == lookup_type::method && methods.is_valid_index(find->method))
                {
                    const asm_method& main_func = methods[find->method];
                    if (signatures.is_valid_index(main_func.signature))
                    {
                        const asm_signature& main_func_sig = signatures[main_func.signature];
                        if (main_func_sig.return_type == type_idx::i32 && main_func_sig.parameters.empty())
                        {
                            main = main_func.index;
                        }
                    }
                }
            }
        }


        subcode resolve_set(type_idx lhs, type_idx rhs) const
        {
            const auto& lhs_type = types[lhs];
            const auto& rhs_type = types[rhs];

            if (lhs_type.is_pointer())
            {
                // Ensure that both pointer types are equal or LHS is a voidpointer
                VALIDATE_IMPLICIT_CONVERSION(lhs_type.index == rhs_type.index || (lhs_type.index == type_idx::vptr && rhs_type.is_pointer()), lhs_type.index, rhs_type.index);

                lhs = rhs = size_type;
            }
            else if (lhs_type.is_signature())
            {
                VALIDATE_IMPLICIT_CONVERSION(lhs_type.index == rhs_type.index || rhs_type.index == type_idx::vptr, lhs_type.index, rhs_type.index);

                lhs = rhs = size_type;
            }
            else if (lhs_type.is_arithmetic())
            {
                // Ensure that both types are arithmetic
                VALIDATE_IMPLICIT_CONVERSION(rhs_type.is_arithmetic(), lhs_type.index, rhs_type.index);
            }
            else if ((lhs_type.is_struct() || lhs_type.is_array()) && lhs_type.index == rhs_type.index)
            {
                // Copy data
                return subcode(45);
            }
            else
            {
                VALIDATE_IMPLICIT_CONVERSION(false, lhs_type.index, rhs_type.index);
            }

            const subcode sub = translate::set(lhs, rhs);
            VALIDATE_IMPLICIT_CONVERSION(sub != subcode::invalid, lhs_type.index, rhs_type.index);
            return sub;
        }
        subcode resolve_conv(type_idx lhs, type_idx rhs) const
        {
            const auto& lhs_type = types[lhs];
            const auto& rhs_type = types[rhs];

            if (lhs_type.is_pointer()) lhs = size_type;
            if (rhs_type.is_pointer()) rhs = size_type;

            // Ensure arithmetic (pointers are treated as size type)
            VALIDATE_EXPLICIT_CONVERSION(is_arithmetic(lhs) && is_arithmetic(rhs) && lhs_type.index != rhs_type.index, lhs_type.index, rhs_type.index);

            const subcode sub = translate::conv(lhs, rhs);
            VALIDATE_EXPLICIT_CONVERSION(sub != subcode::invalid, lhs_type.index, rhs_type.index);
            return sub;
        }
        subcode resolve_ari(opcode op, type_idx lhs, type_idx rhs) const
        {
            const auto& lhs_type = types[lhs];
            const auto& rhs_type = types[rhs];

            // Ensure arithmetic
            VALIDATE_ARITHMETIC_EXPRESSION(lhs_type.is_arithmetic() && rhs_type.is_arithmetic(), lhs_type.index, rhs_type.index);

            const subcode sub = translate::ari(op, lhs, rhs);
            VALIDATE_ARITHMETIC_EXPRESSION(sub != subcode::invalid, lhs_type.index, rhs_type.index);
            return sub;
        }
        subcode resolve_cmp(opcode op, type_idx lhs, type_idx rhs) const
        {
            const auto& lhs_type = types[lhs];
            const auto& rhs_type = types[rhs];

            if (lhs_type.is_pointer())
            {
                // Pointer types must be equal for valid comparison
                VALIDATE_COMPARISON_EXPRESSION(lhs_type.index == rhs_type.index, lhs_type.index, rhs_type.index);

                lhs = rhs = size_type;
            }
            else
            {
                // Ensure arithmetic
                VALIDATE_COMPARISON_EXPRESSION(lhs_type.is_arithmetic() && rhs_type.is_arithmetic(), lhs_type.index, rhs_type.index);
            }

            const subcode sub = translate::cmp(op, lhs, rhs);
            VALIDATE_COMPARISON_EXPRESSION(sub != subcode::invalid, lhs_type.index, rhs_type.index);
            return sub;
        }
        subcode resolve_ptr(opcode op, type_idx lhs, type_idx rhs) const
        {
            const auto& lhs_type = types[lhs];
            const auto& rhs_type = types[rhs];

            // Lhs must be a pointer, lhs cannot be a void pointer and rhs must be integral
            VALIDATE_POINTER_EXPRESSION(lhs_type.is_pointer() && lhs_type.index != type_idx::vptr && rhs_type.is_integral(), lhs_type.index, rhs_type.index);

            const subcode sub = translate::ptr(op, lhs, rhs);
            VALIDATE_POINTER_EXPRESSION(sub != subcode::invalid, lhs_type.index, rhs_type.index);
            return sub;
        }
        void resolve_pdif(type_idx lhs, type_idx rhs) const
        {
            const auto& lhs_type = types[lhs];
            const auto& rhs_type = types[rhs];

            // Lhs must be a pointer, lhs cannot be a void pointer and rhs must be integral
            VALIDATE_PTR_OFFSET_EXPRESSION(lhs_type.is_pointer() && lhs_type.index != type_idx::vptr && lhs_type.index == rhs_type.index, lhs_type.index, rhs_type.index);
        }

        global_idx resolve_method_constant(asm_method& method)
        {
            auto find = method_ptr_lookup.find(method.name);
            if (find == method_ptr_lookup.end())
            {
                VALIDATE_METHOD_DEFINITION(method.is_defined(), get_name(method));
                auto& signature = signatures[method.signature];
                type_idx signature_type_idx;
                if (signature.signature_type == type_idx::invalid)
                {
                    // Create the signature type in case it does not exist
                    gen_type signature_type = gen_type(name_idx::invalid, type_idx(types.size()));
                    signature_type.flags = extended_flags::is_defined | extended_flags::is_resolved;
                    signature_type.total_size = ptr_size;
                    signature_type.make_signature(signature.index);
                    signature.signature_type = signature_type.index;
                    signature_type_idx = signature_type.index;
                    types.push_back(std::move(signature_type));
                }
                else
                {
                    signature_type_idx = signature.signature_type;
                }

                const global_idx global_index = global_idx(constants.info.size()) | global_flags::constant_flag;

                // Create global from method address
                const size_t current_size = constants.data.size();
                constants.data.resize(current_size + sizeof(name_idx) + sizeof(uint16_t) + 1);
                uint8_t* addr = constants.data.data() + current_size;
                write_bytecode<uint16_t>(addr, 1);
                write_bytecode<uint8_t>(addr, static_cast<uint8_t>(type_idx::voidtype));
                write_bytecode<name_idx>(addr, method.name);
                constants.info.push_back(field(method.name, signature_type_idx, current_size));
                method_ptr_lookup.emplace(method.name, global_index);

                return global_index;
            }
            return find->second | global_flags::constant_flag;
        }


        void initialize_data_table(asm_data_table& table, bool is_constant)
        {
            // Initialize global fields
            vector<uint8_t> new_data;
            for (auto& global : table.info)
            {
                const asm_type& global_type = types[global.type];

                const size_t current_size = new_data.size();
                new_data.resize(current_size + global_type.total_size);

                uint8_t* lhs_addr = new_data.data() + current_size;
                memset(lhs_addr, 0, global_type.total_size);
                type_idx lhs_type = global_type.index;

                const uint8_t* rhs_addr = table.data.data() + global.offset;
                const uint16_t init_count = read_bytecode<uint16_t>(rhs_addr);
                uint16_t used_count = init_count;
                global.offset = current_size;

                initialize_data_recursive(global.name, lhs_addr, global_type.index, rhs_addr, used_count, is_constant);

                // Ensure that we don't have unused initializer values
                VALIDATE_GLOBAL_INITIALIZER_COUNT(used_count == 0, init_count, (init_count - used_count), get_name(global.name));
            }
            swap(table.data, new_data);
        }
        void initialize_data_recursive(name_idx name, uint8_t*& lhs_addr, type_idx lhs_type, const uint8_t*& rhs_addr, uint16_t& init_count, bool is_constant)
        {
            const auto& t = types[lhs_type];

            if (t.is_arithmetic() || t.is_pointer())
            {
                // Arithmetic/pointer initialization
                const auto lhs_size = types[lhs_type].total_size;
                if (init_count > 0)
                {
                    const type_idx init_type = type_idx(read_bytecode<uint8_t>(rhs_addr));
                    if (init_type == type_idx::voidtype)
                    {
                        // Currently, globals are not valid initializers for constants
                        const name_idx identifier = read_bytecode<name_idx>(rhs_addr);
                        ASSERT(false, "Invalid constant initialization");
                    }
                    else
                    {
                        // Implicit conv from encoded constant
                        type_idx rhs_type = init_type;
                        if (types[lhs_type].is_pointer()) lhs_type = size_type;
                        if (types[rhs_type].is_pointer()) rhs_type = size_type;

                        ASSERT(types[rhs_type].is_arithmetic(), "Invalid constant initialization");

                        operations::conv(lhs_addr, lhs_type, rhs_addr, rhs_type);

                        rhs_addr += types[init_type].total_size;
                    }
                    init_count--;
                }
                lhs_addr += lhs_size;
            }
            else if (t.is_signature())
            {
                VALIDATE_METHOD_PTR_INITIALIZER(!is_constant || init_count > 0, get_name(name));

                // Method pointer initialization
                if (init_count > 0)
                {
                    // Initialize signature (find method constant)
                    const type_idx init_type = type_idx(read_bytecode<uint8_t>(rhs_addr));
                    if (init_type == type_idx::vptr)
                    {
                        // nullptr initialiation for signatures
                        write_bytecode<size_t>(lhs_addr, 0);
                    }
                    else if (init_type == type_idx::voidtype)
                    {
                        const name_idx identifier = read_bytecode<name_idx>(rhs_addr);

                        // Method initialization for signatures
                        auto find = database[identifier];
                        ASSERT(find, "Invalid identifier");
                        VALIDATE_METHOD_INITIALIZER_DEFINITION(find->lookup == lookup_type::method, get_name(name), find.name);

                        write_bytecode<size_t>(lhs_addr, (static_cast<size_t>(find->method) ^ runtime_hash));
                    }
                    else
                    {
                        VALIDATE_METHOD_INITIALIZER(false, get_name(name));
                    }

                    init_count--;
                }
                else
                {
                    write_bytecode<size_t>(lhs_addr, 0);
                }
            }
            else if (t.is_array())
            {
                // Initialize array
                for (size_t i = 0; i < t.generated.array.array_size; i++)
                {
                    initialize_data_recursive(name, lhs_addr, t.generated.array.underlying_type, rhs_addr, init_count, is_constant);
                }
            }
            else
            {
                // Initialize fields
                for (size_t i = 0; i < t.fields.size(); i++)
                {
                    initialize_data_recursive(name, lhs_addr, t.fields[i].type, rhs_addr, init_count, is_constant);
                }
            }
        }

        // Name helper functions
        inline string_view get_name(name_idx name) const
        {
            ASSERT(database.is_valid_index(name), "Name index out of range");
            return database[name].name;
        }
        inline string_view get_name(type_idx type) const
        {
            size_t& index = const_cast<size_t&>(generated_name_index);
            string& buffer = const_cast<string*>(generated_name_buffers)[index];
            name_generator(type, buffer, types, signatures, database);
            index = (index + 1) & 1;
            return buffer;
        }
        inline string_view get_name(const asm_type& type) const
        {
            return get_name(type.index);
        }
        inline string_view get_name(method_idx method) const
        {
            const name_idx name = methods[method].name;
            ASSERT(database.is_valid_index(name), "Name index out of range");
            return database[name].name;
        }
        inline string_view get_name(const asm_method& method) const
        {
            return get_name(method.name);
        }


        gen_intermediate_data data;

        const type_idx size_type;
        const type_idx offset_type;
        const size_t ptr_size;

        asm_method* current_method = nullptr;
        asm_signature* current_signature = nullptr;
        inline void set_return_value(type_idx type)
        {
            const size_t size = data.types[type].total_size;
            if (size > max_return_value_size) max_return_value_size = size;
            return_value = type;
        }
        inline void clear_return_value()
        {
            return_value = type_idx::voidtype;
        }
        type_idx return_value = type_idx::invalid;
        size_t max_return_value_size = 0;
        uint8_t* iptr = nullptr;
        uint32_t iidx = 0;
        opcode current_op = opcode::noop;

        vector<uint32_t> labels;
        uint32_t label_idx = 0;

        string generated_name_buffers[2];
        size_t generated_name_index = 0;

        unordered_map<name_idx, global_idx> method_ptr_lookup;
    };

    void asm_assembly_data::serialize(assembly& dst, const asm_assembly_data& data)
    {
        block_writer writer;
        writer.write_direct(constants::assembly_header);
        writer.write_direct(toolchain_version::current());
        writer.write(data);
        vector<uint8_t> serialized = writer.finalize();
        append_bytecode(serialized, constants::footer);

        dst.content = block<uint8_t>(serialized.data(), serialized.size());
    }

    assembly::assembly(const intermediate& im, const runtime& runtime)
    {
        VALIDATE_INTERMEDIATE(im.is_valid());
        VALIDATE_COMPATIBILITY(im.is_compatible());

        gen_intermediate_data data = gen_intermediate_data::deserialize(im);

        asm_assembly_data::serialize(*this, assembly_linker(std::move(data), runtime));
    }
    assembly::assembly(const intermediate& im) : assembly(im, runtime())
    {

    }
}