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
    runtime::runtime(span<const library> libs)
    {
        auto& data = self();

        toolchain_version version = toolchain_version::current();
        data.hash = fnv::hash(&version, sizeof(toolchain_version));

        for (auto& lib : libs)
        {
            *this += lib;
        }
    }
    runtime::runtime(const library& lib) : runtime(init_span(lib))
    {

    }
    runtime::~runtime()
    {

    }

    runtime& runtime::operator+=(const library& lib)
    {
        auto& lib_data = lib.self();

        if (!lib_data.calls.empty())
        {
            auto& data = self();

            auto find_lib = data.libraries.find(lib_data.path);
            ASSERT(!find_lib, "Duplicate library");
            find_lib = data.libraries.emplace(lib_data.path, lib_data.preload_symbols, lib_data.calls);

            data.hash = fnv::append(data.hash, lib_data.hash);

            index_t idx = 0;
            for (auto& call : find_lib->calls)
            {
                data.call_lookup.emplace(call.name, runtime_data::call_index(find_lib.key, idx++));
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
        }

        return *this;
    }
}