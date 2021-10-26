#include "runtime.hpp"
#include "errors.hpp"

#define _BASE_TYPE_SIZE_CHECK(t, s) static_assert(propane::get_base_type_size(propane::type_idx::t) == s, "Size check for type " #t " failed (sizeof(" #t ") == " #s ")")
_BASE_TYPE_SIZE_CHECK(i8, 1);
_BASE_TYPE_SIZE_CHECK(u8, 1);
_BASE_TYPE_SIZE_CHECK(i16, 2);
_BASE_TYPE_SIZE_CHECK(u16, 2);
_BASE_TYPE_SIZE_CHECK(i32, 4);
_BASE_TYPE_SIZE_CHECK(u32, 4);
_BASE_TYPE_SIZE_CHECK(i64, 8);
_BASE_TYPE_SIZE_CHECK(u64, 8);
_BASE_TYPE_SIZE_CHECK(f32, 4);
_BASE_TYPE_SIZE_CHECK(f64, 8);
_BASE_TYPE_SIZE_CHECK(vptr, sizeof(propane::vptr));
_BASE_TYPE_SIZE_CHECK(voidtype, 0);

static_assert(sizeof(char) == 1, "Size of char is expected to be 1");

namespace propane
{
    environment::environment(span<const library> libs)
    {
        for (const auto& lib : libs)
        {
            *this += lib;
        }
    }
    environment::environment(const library& lib)
    {
        *this += lib;
    }

    environment& environment::operator+=(const library& lib)
    {
        auto& data = self();
        auto& lib_data = lib.self();

        ASSERT(!data.libraries.contains(lib_data.path), "Duplicate library entry");
        data.libraries[lib_data.path] = &lib_data;
        
        return *this;
    }
    environment::~environment()
    {

    }


    runtime::runtime()
    {
        auto& data = self();

        toolchain_version version = toolchain_version::current();
        self().hash = fnv::hash(&version, sizeof(toolchain_version));
    }
    runtime::runtime(const environment& env) : runtime()
    {
        auto& data = self();
        auto& env_data = env.self();

        index_t lib_idx = 0;
        for (auto& pair : env_data.libraries)
        {
            auto& lib_data = *pair.second;

            data.hash = fnv::append(data.hash, lib_data.hash);
            
            library_info add_lib(lib_data.path, lib_data.preload_symbols, lib_data.calls);

            const name_idx lib_name = name_idx(lib_idx++);
            index_t call_idx = 0;
            for (auto& call : add_lib.calls)
            {
                data.call_lookup.emplace(call.name, runtime_call_index(lib_name, call_idx++));
            }
            
            for (auto& type : lib_data.types)
            {
                auto find_type = data.type_lookup.find(type.type);
                if (find_type == data.type_lookup.end())
                {
                    data.type_lookup.emplace(type.type, type);
                }
                else
                {
                    ASSERT(find_type->second.size == type.size, "Native type size mismatch");
                }
            }

            data.libraries.push_back(std::move(add_lib));
        }
    }
    runtime::~runtime()
    {

    }
}