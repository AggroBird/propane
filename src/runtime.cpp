#include "runtime.hpp"
#include "errors.hpp"

#define _BASE_TYPE_SIZE_CHECK(t, s) static_assert(propane::get_base_type_size(propane::type_idx::t) == s, "Size check for type " #t " failed (sizeof(" #t ") == " #s ")")
_BASE_TYPE_SIZE_CHECK(i8, 1);
_BASE_TYPE_SIZE_CHECK(u8, 1);
_BASE_TYPE_SIZE_CHECK(i16, 2);
_BASE_TYPE_SIZE_CHECK(u16, 2);
_BASE_TYPE_SIZE_CHECK(i32, 4);
_BASE_TYPE_SIZE_CHECK(u32, 4);
_BASE_TYPE_SIZE_CHECK(i32, 4);
_BASE_TYPE_SIZE_CHECK(u32, 4);
_BASE_TYPE_SIZE_CHECK(i64, 8);
_BASE_TYPE_SIZE_CHECK(u64, 8);
_BASE_TYPE_SIZE_CHECK(vptr, sizeof(propane::vptr));
_BASE_TYPE_SIZE_CHECK(voidtype, 0);

static_assert(sizeof(char) == 1, "Size of char is expected to be 1");

namespace propane
{
    template<typename value_t> struct sort_named
    {
        constexpr bool operator()(const value_t& lhs, const value_t& rhs) const noexcept
        {
            return lhs.name < rhs.name;
        }
    };

    runtime::runtime(span<const library> libs)
    {
        auto& data = self();

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
        auto& data = self();

        if (!lib_data.calls.empty())
        {
            data.set_dirty();

            auto find = data.libraries.find(lib_data.path);
            if (!find)
            {
                find = data.libraries.emplace(lib_data.path, lib_data.path, lib_data.preload_symbols);
            }
            else
            {
                find->preload_symbols |= lib_data.preload_symbols;
            }

            data.calls.reserve(data.calls.size() + lib_data.calls.size());
            for (auto& call : lib_data.calls)
            {
                external_call_info copy = call;
                copy.library = find.key;
                data.calls.push_back(std::move(copy));
            }
        }

        return *this;
    }

    size_t runtime_data::rehash() noexcept
    {
        if (modified)
        {
            modified = false;

            toolchain_version version = toolchain_version::current();
            hash_value = fnv::hash(&version, sizeof(version));

            std::sort(calls.begin(), calls.end(), sort_named<external_call_info>{});

            index_t idx = 0;
            call_lookup.clear();
            for (auto& call : calls)
            {
                hash_value = fnv::append(hash_value, call.name);
                hash_value = fnv::append(hash_value, call.return_type);
                for (const auto& it : call.parameters) hash_value = fnv::append(hash_value, it.type);

                call.index = idx++;
                call_lookup.emplace(call.name, call.index);
            }
        }
        return hash_value;
    }

    void runtime_data::set_dirty()
    {
        modified = true;
        hash_value = 0;
    }

    size_t runtime::hash() const noexcept
    {
        return const_cast<runtime_data&>(self()).rehash();
    }
}