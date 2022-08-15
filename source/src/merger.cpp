#include "intermediate_data.hpp"
#include "errors.hpp"
#include "utility.hpp"

#define VALIDATE(errc, expr, ...) ENSURE(errc, expr, propane::merger_exception, __VA_ARGS__)

#define VALIDATE_INTERMEDIATE(expr) VALIDATE(ERRC::MRG_INVALID_INTERMEDIATE, expr, \
    "Attempted to merge an invalid intermediate")
#define VALIDATE_COMPATIBILITY(expr) VALIDATE(ERRC::MRG_INCOMPATIBLE_INTERMEDIATE, expr, \
    "Attempted to merge an intermediate that was build using an incompatible toolchain")
#define VALIDATE_INDEX(idx, max) VALIDATE(ERRC::MRG_INDEX_OUT_OF_RANGE, size_t(idx) < size_t(max), \
    "% out of range", get_index_type_name(idx))
#define VALIDATE_TYPE_DEF(expr, name, lhs_meta, rhs_meta) VALIDATE(ERRC::MRG_TYPE_REDEFINITION, expr, \
    "Type '%' (%) has already been defined (see %)", name, lhs_meta, rhs_meta)
#define VALIDATE_METHOD_DEF(expr, name, lhs_meta, rhs_meta) VALIDATE(ERRC::MRG_METHOD_REDEFINITION, expr, \
    "Method '%' (%) has already been defined (see %)", name, lhs_meta, rhs_meta)
#define VALIDATE_GLOBAL_DEF(expr, name) VALIDATE(ERRC::MRG_GLOBAL_REDEFINITION, expr, \
    "Global '%' has already been defined", name)
#define VALIDATE_IDENTIFIER_TYPE(lhs_type, rhs_type) VALIDATE(ERRC::MRG_IDENTIFIER_TYPE_MISMATCH, false, \
    "Definition of % '%' collides with previous % definition", lhs_type, (find).name, rhs_type)
#define VALIDATE_IDENTIFIER_TYPE_WITH_META(lhs_type, lhs_meta, rhs_type, rhs_meta) VALIDATE(ERRC::MRG_IDENTIFIER_TYPE_MISMATCH, false, \
    "Definition of % '%' (%) collides with previous % definition (see %)", lhs_type, rhs_type, lhs_meta, rhs_type, rhs_meta)

namespace propane
{
    template<typename value_t> inline bool translate_value(const indexed_block<value_t, value_t>& translations, value_t& key)
    {
        const auto dst = translations[key];
        if (dst != key)
        {
            key = dst;
            return true;
        }
        return false;
    }

    // Intermediate merger takes in two intermediates and produces
    // the merged result
    class merger final : public gen_intermediate_data
    {
    public:
        NOCOPY_CLASS_DEFAULT(merger) = delete;

        merger(gen_intermediate_data&& lhs, gen_intermediate_data&& rhs) :
            gen_intermediate_data(std::move(lhs)), merge(rhs)
        {
            restore_lookup_tables();
            restore_generated_types();

            keybuf.reserve(32);

            initialize_translations(type_translations, merge.types.size());
            initialize_translations(method_translations, merge.methods.size());
            initialize_translations(signature_translations, merge.signatures.size());
            initialize_translations(offset_translations, merge.offsets.size());
            if (!merge.database.empty())
            {
                name_translations = indexed_block<name_idx, name_idx>(merge.database.size());
                for (size_t i = 0; i < name_translations.size(); i++)
                {
                    const name_idx index = name_idx(i);
                    const string_view identifier = merge.database[index].name;
                    auto find = database.find(identifier);
                    name_translations[index] = find ? find.key : database.emplace(identifier, lookup_idx::make_identifier()).key;
                }
            }
            if (!merge.metatable.empty())
            {
                meta_translations = indexed_block<meta_idx, meta_idx>(merge.metatable.size());
                for (size_t i = 0; i < meta_translations.size(); i++)
                {
                    const meta_idx index = meta_idx(i);
                    const string_view metastr = merge.metatable[index].name;
                    auto find = metatable.find(metastr);
                    meta_translations[index] = find != meta_idx::invalid ? find : metatable.emplace(metastr);
                }
            }

            // Merge defined and declared types
            vector<type_idx> untranslated_types;
            size_t next_index = types.size();
            for (auto& src : merge.types)
            {
                if (is_base_type(src.index)) continue;

                // This gets regenerated
                src.pointer_type = type_idx::invalid;
                src.array_types.clear();

                if (src.is_generated()) continue;

                const type_idx src_type = src.index;
                if (auto find = lookup(src.name, lookup_type::type, src_type))
                {
                    auto& dst = types[find->type];

                    VALIDATE_TYPE_DEF(!dst.is_defined() || !src.is_defined(), database[dst.name].name, make_meta(dst.index), merge.make_meta(src.index));

                    src.index = dst.index;
                    src.name = dst.name;
                }
                else
                {
                    src.index = type_idx(next_index++);
                    src.name = name_translations[src.name];
                    *database[src.name] = src.index;
                }
                type_translations[src_type] = src.index;
            }
            for (auto& src : merge.types)
            {
                if (is_base_type(src.index)) continue;
                if (src.is_generated()) continue;

                if (size_t(src.index) == types.size())
                {
                    if (src.is_defined()) untranslated_types.push_back(src.index);

                    types.push_back(std::move(src));
                }
                else
                {
                    VALIDATE_INDEX(src.index, types.size());

                    auto& dst = types[src.index];

                    if (!dst.is_defined() && src.is_defined())
                    {
                        untranslated_types.push_back(src.index);

                        // Make sure we don't overwrite the pointer references
                        dst.fields = std::move(src.fields);
                        dst.flags = src.flags;
                        dst.flags |= extended_flags::is_defined;
                    }
                }
            }

            // Merge generated types
            for (auto& src : merge.types)
            {
                if (is_base_type(src.index)) continue;
                if (!src.is_generated()) continue;

                if (src.is_pointer())
                {
                    translate(src.generated.pointer.underlying_type);

                    auto& base_type = types[src.generated.pointer.underlying_type];
                    if (base_type.pointer_type == type_idx::invalid)
                    {
                        const type_idx src_idx = src.index;

                        // Pointer type not yet defined
                        src.index = type_idx(next_index++);
                        base_type.pointer_type = src.index;
                        types.push_back(std::move(src));
                        type_translations[src_idx] = src.index;
                    }
                    else if (src.index != base_type.pointer_type)
                    {
                        // Pointer type already defined, add it to the translations
                        type_translations[src.index] = base_type.pointer_type;
                    }
                }
                else if (src.is_array())
                {
                    translate(src.generated.array.underlying_type);

                    auto& base_type = types[src.generated.array.underlying_type];
                    auto find = base_type.array_types.find(src.generated.array.array_size);
                    if (find == base_type.array_types.end())
                    {
                        const type_idx src_idx = src.index;

                        // Array type not yet defined
                        src.index = type_idx(next_index++);
                        base_type.array_types.emplace(src.generated.array.array_size, src.index);
                        types.push_back(std::move(src));
                        type_translations[src_idx] = src.index;
                    }
                    else if (src.index != find->second)
                    {
                        // Array type already defined, add it to the translations
                        type_translations[src.index] = find->second;
                    }
                }
                else if (src.is_signature())
                {
                    // Merge the signature first if needed
                    signature_idx dst_idx = src.generated.signature.index;
                    const signature_idx src_idx = src.generated.signature.index;
                    src.generated.signature.index = dst_idx = merge_signature(std::move(merge.signatures[src_idx]));
                    signature_translations[src_idx] = dst_idx;

                    auto& signature = signatures[dst_idx];
                    if (signature.signature_type == type_idx::invalid)
                    {
                        const type_idx src_type = src.index;

                        // Signature type not yet defined
                        src.index = type_idx(next_index++);
                        signature.signature_type = src.index;
                        types.push_back(std::move(src));
                        type_translations[src_type] = src.index;
                    }
                    else if (src.index != signature.signature_type)
                    {
                        // Signature type already defined, add it to the translations
                        type_translations[src.index] = signature.signature_type;
                    }
                }
                else
                {
                    ASSERT(false, "Unhandled generated type case");
                }
            }

            // Translate types
            for (auto& idx : untranslated_types)
            {
                auto& t = types[idx];
                for (auto& f : t.fields)
                {
                    rename(f.name);
                    translate(f.type);
                }
                translate(t.meta.index);
            }

            // Merge remaining signatures
            for (auto& signature : merge.signatures)
            {
                if (signature.index != signature_idx::invalid)
                {
                    const signature_idx src_idx = signature.index;
                    const signature_idx dst_idx = merge_signature(std::move(signature));
                    signature_translations[src_idx] = dst_idx;
                }
            }

            // Merge offsets
            for (size_t i = 0; i < merge.offsets.size(); i++)
            {
                const offset_idx src_idx = offset_idx(i);
                auto& offset = merge.offsets[src_idx];

                translate(offset.name.object_type);
                for (auto& fn : offset.name.field_names) rename(fn);

                offset.name.make_key(keybuf);
                auto find = offset_lookup.find(keybuf);
                if (find == offset_lookup.end())
                {
                    const offset_idx dst_idx = offset_idx(offsets.size());
                    offset_lookup.emplace(keybuf, dst_idx);
                    offsets.push_back(std::move(offset));
                    offset_translations[src_idx] = dst_idx;
                }
                else
                {
                    offset_translations[src_idx] = find->second;
                }
            }

            // Fold globals
            merge_data_table(globals, merge.globals, lookup_type::global);
            merge_data_table(constants, merge.constants, lookup_type::constant);

            // Merge methods
            vector<method_idx> untranslated_methods;
            next_index = methods.size();
            for (auto& src : merge.methods)
            {
                const method_idx src_type = src.index;
                if (auto find = lookup(src.name, lookup_type::method, src_type))
                {
                    auto& dst = methods[find->method];

                    VALIDATE_METHOD_DEF(!dst.is_defined() || !src.is_defined(), database[dst.name].name, make_meta(dst.index), merge.make_meta(src.index));

                    src.index = dst.index;
                    src.name = dst.name;
                }
                else
                {
                    src.index = method_idx(next_index++);
                    src.name = name_translations[src.name];
                    *database[src.name] = src.index;
                }
                method_translations[src_type] = src.index;
            }
            for (auto& src : merge.methods)
            {
                if (size_t(src.index) == methods.size())
                {
                    if (src.is_defined()) untranslated_methods.push_back(src.index);

                    methods.push_back(std::move(src));
                }
                else
                {
                    VALIDATE_INDEX(src.index, methods.size());

                    auto& dst = methods[src.index];

                    if (!dst.is_defined() && src.is_defined())
                    {
                        untranslated_methods.push_back(src.index);

                        dst = std::move(src);
                    }
                }
            }

            // Translate methods
            for (auto& idx : untranslated_methods)
            {
                auto& m = methods[idx];
                for (auto& sv : m.stackvars) translate(sv.type);
                for (auto& c : m.calls) translate(c);
                for (auto& o : m.offsets) translate(o);
                for (auto& g : m.globals) rename(g.name);
                translate(m.signature);
                translate(m.meta.index);
            }
        }

        gen_intermediate_data& merge;

        indexed_block<type_idx, type_idx> type_translations;
        indexed_block<method_idx, method_idx> method_translations;
        indexed_block<signature_idx, signature_idx> signature_translations;
        indexed_block<offset_idx, offset_idx> offset_translations;
        indexed_block<name_idx, name_idx> name_translations;
        indexed_block<meta_idx, meta_idx> meta_translations;

        vector<uint8_t> keybuf;

        template<typename value_t> inline void initialize_translations(indexed_block<value_t, value_t>& block, size_t num)
        {
            if (num > 0)
            {
                block = indexed_block<value_t, value_t>(num);
                for (size_t i = 0; i < block.size(); i++)
                {
                    block[value_t(i)] = value_t(i);
                }
            }
        }

        template<typename value_t> inline find_result<name_idx, lookup_idx> lookup(name_idx src_name, lookup_type type, value_t index)
        {
            VALIDATE_INDEX(src_name, name_translations.size());
            auto find = database[name_translations[src_name]];
            if (find->lookup == lookup_type::identifier)
            {
                return find_result<name_idx, lookup_idx>();
            }
            if (find->lookup != type)
            {
                if (type == lookup_type::type && find->lookup == lookup_type::method)
                {
                    VALIDATE_IDENTIFIER_TYPE_WITH_META(type, merge.make_meta(type_idx(index)), find->lookup, make_meta(find->method));
                }
                else if (type == lookup_type::method && find->lookup == lookup_type::type)
                {
                    VALIDATE_IDENTIFIER_TYPE_WITH_META(type, merge.make_meta(method_idx(index)), find->lookup, make_meta(find->type));
                }
                else
                {
                    VALIDATE_IDENTIFIER_TYPE(type, find->lookup);
                }
            }
            return find;
        }
        inline find_result<name_idx, lookup_idx> lookup(name_idx src_name, lookup_type type)
        {
            return lookup(src_name, type, 0);
        }

        signature_idx merge_signature(gen_signature&& signature)
        {
            translate(signature.return_type);
            for (auto& p : signature.parameters) translate(p.type);

            signature.signature_type = type_idx::invalid;

            signature.make_key(keybuf);
            auto find = signature_lookup.find(keybuf);
            if (find == signature_lookup.end())
            {
                const signature_idx dst_idx = signature_idx(signatures.size());
                signature.index = dst_idx;
                signatures.push_back(std::move(signature));
                signature_lookup.emplace(keybuf, dst_idx);
                signature.index = signature_idx::invalid;
                return dst_idx;
            }
            signature.index = signature_idx::invalid;
            return find->second;
        }

        void merge_data_table(gen_data_table& dst, gen_data_table& src, lookup_type type)
        {
            const size_t current_data_size = dst.data.size();
            for (auto& global : src.info)
            {
                auto find = lookup(global.name, type);
                VALIDATE_GLOBAL_DEF(!find, merge.database[global.name].name);

                const index_t global_idx = index_t(dst.info.size());

                global.name = name_translations[global.name];
                *database[global.name] = lookup_idx(type, global_idx);

                // Translate identifier if any
                pointer_t addr = src.data.data() + global.offset;
                const uint16_t init_count = read_bytecode<uint16_t>(addr);
                for (uint16_t i = 0; i < init_count; i++)
                {
                    const type_idx init_type = type_idx(read_bytecode<uint8_t>(addr));
                    if (init_type == type_idx::voidtype)
                        rename(read_bytecode_ref<name_idx>(addr));
                    else
                        addr += get_base_type_size(init_type);
                }

                translate(global.type);
                *global.offset += current_data_size;
                dst.info.push_back(global);
            }
            dst.data.insert(dst.data.end(), src.data.begin(), src.data.end());
        }

        inline void rename(name_idx& name)
        {
            VALIDATE_INDEX(name, name_translations.size());
            name = name_translations[name];
        }
        inline bool translate(type_idx& type)
        {
            VALIDATE_INDEX(type, type_translations.size());
            return translate_value(type_translations, type);
        }
        inline bool translate(method_idx& method)
        {
            VALIDATE_INDEX(method, method_translations.size());
            return translate_value(method_translations, method);
        }
        inline bool translate(signature_idx& signature)
        {
            VALIDATE_INDEX(signature, signature_translations.size());
            return translate_value(signature_translations, signature);
        }
        inline bool translate(offset_idx& offset)
        {
            VALIDATE_INDEX(offset, offset_translations.size());
            return translate_value(offset_translations, offset);
        }
        inline bool translate(meta_idx& meta)
        {
            if (meta == meta_idx::invalid) return false;
            VALIDATE_INDEX(meta, meta_translations.size());
            return translate_value(meta_translations, meta);
        }
    };

    gen_intermediate_data gen_intermediate_data::merge(gen_intermediate_data&& lhs_data, gen_intermediate_data&& rhs_data)
    {
        ASSERT(lhs_data.types.size() >= base_type_count(), "Merge destination does not have base types set up");

        return merger(std::move(lhs_data), std::move(rhs_data));
    }
    gen_intermediate_data gen_intermediate_data::merge(const intermediate& lhs, const intermediate& rhs)
    {
        VALIDATE_INTERMEDIATE(lhs.is_valid());
        VALIDATE_INTERMEDIATE(rhs.is_valid());
        VALIDATE_COMPATIBILITY(lhs.is_compatible());
        VALIDATE_COMPATIBILITY(rhs.is_compatible());

        auto lhs_data = gen_intermediate_data::deserialize(lhs);
        auto rhs_data = gen_intermediate_data::deserialize(rhs);

        return merge(std::move(lhs_data), std::move(rhs_data));
    }
}