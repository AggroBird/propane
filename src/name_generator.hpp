#ifndef _HEADER_NAME_GENERATOR
#define _HEADER_NAME_GENERATOR

#include "errors.hpp"
#include "database.hpp"

namespace propane
{
    // Type name generator
    template<typename type_list_t, typename signature_list_t, typename database_t> struct name_generator
    {
        name_generator(type_idx type, std::string& out_name, const type_list_t& types, const signature_list_t& signatures, const database_t& database) :
            types(types),
            signatures(signatures),
            database(database)
        {
            out_name.clear();
            const bool result = generate_recursive(type, out_name);
            ASSERT(result, "Failed to generate name");
        }

    private:
        const type_list_t& types;
        const signature_list_t& signatures;
        const database_t& database;

        bool generate_recursive(type_idx type, std::string& out_name) const
        {
            if (types.is_valid_index(type))
            {
                const auto& t = types[type];

                if (t.is_pointer())
                {
                    // Generate pointer name
                    if (generate_recursive(t.generated.pointer.underlying_type, out_name))
                    {
                        out_name.push_back('*');
                        return true;
                    }
                }
                else if (t.is_array())
                {
                    // Generate array name
                    if (generate_recursive(t.generated.array.underlying_type, out_name))
                    {
                        out_name.push_back('[');
                        out_name.append(std::to_string(t.generated.array.array_size));
                        out_name.push_back(']');
                        return true;
                    }
                }
                else if (t.is_signature())
                {
                    // Generate signature name
                    const auto& signature = signatures[t.generated.signature.index];
                    if (generate_recursive(signature.return_type, out_name))
                    {
                        out_name.push_back('(');
                        for (size_t i = 0; i < signature.parameters.size(); i++)
                        {
                            if (i != 0) out_name.push_back(',');
                            if (!generate_recursive(signature.parameters[i].type, out_name))
                                return false;
                        }
                        out_name.push_back(')');
                        return true;
                    }
                }
                else if (database.is_valid_index(t.name))
                {
                    // Generate name from identifier
                    out_name.append(get_database_entry(database, t.name));
                    return true;
                }
            }

            return false;
        }
    };
}

#endif