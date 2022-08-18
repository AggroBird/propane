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
        auto& self_data = self();
        auto& lib_data = lib.self();

        ASSERT(self_data.libraries.find(lib_data.path) == self_data.libraries.end(), "Duplicate library entry");
        self_data.libraries[lib_data.path] = &lib_data;
        
        return *this;
    }
    environment::~environment()
    {

    }


    runtime::runtime()
    {
        auto& self_data = self();
        toolchain_version version = toolchain_version::current();
        self_data.hash = fnv::hash(&version, sizeof(toolchain_version));
    }
    runtime::runtime(const environment& env) : runtime()
    {
        auto& self_data = self();
        auto& env_data = env.self();

        uint32_t lib_idx = 0;
        for (auto& pair : env_data.libraries)
        {
            auto& lib_data = *pair.second;

            self_data.hash = fnv::append(self_data.hash, lib_data.hash);
            
            library_info add_lib(lib_data.path, lib_data.preload_symbols, lib_data.calls);

            const name_idx lib_name = name_idx(lib_idx++);
            uint32_t call_idx = 0;
            for (auto& call : add_lib.calls)
            {
                self_data.call_lookup.emplace(call.name, runtime_call_index(lib_name, call_idx++));
            }
            
            for (auto& type : lib_data.types)
            {
                auto find_type = self_data.type_lookup.find(type.type);
                if (find_type == self_data.type_lookup.end())
                {
                    self_data.type_lookup.emplace(type.type, type);
                }
                else
                {
                    ASSERT(find_type->second.size == type.size, "Native type size mismatch");
                }
            }

            self_data.libraries.push_back(std::move(add_lib));
        }
    }
    runtime::~runtime()
    {

    }
}