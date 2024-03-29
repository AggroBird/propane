#include "library.hpp"
#include "errors.hpp"
#include "utility.hpp"

namespace propane
{
    external_call::external_call(std::string_view name) :
        name(name) {}

    library::library(string_view path, bool preload_symbols, span<const external_call> calls) : handle(path, preload_symbols)
    {
        auto& self_data = self();

        self_data.calls = block<external_call_info>(calls.size());
        external_call_info* write_call = self_data.calls.data();
        unordered_map<string_view, native::typedecl> declared_types;
        for (auto& call : calls)
        {
            ASSERT(is_identifier(call.name), "Invalid name");

            for (auto& type : call.parameters)
            {
                ASSERT(is_identifier(type.name), "Invalid name");

                declared_types.emplace(type.name, type);
            }

            ASSERT(is_identifier(call.return_type.name), "Invalid name");

            declared_types.emplace(call.return_type.name, call.return_type);

            external_call_info info(call.name);
            info.return_type = call.return_type;
            info.parameters = call.parameters;
            info.parameters_size = call.parameters_size;
            info.forward = call.forward;
            info.handle = call.handle;
            *write_call++ = std::move(info);
        }

        self_data.types = block<native::typedecl>(declared_types.size());
        auto* write_type = self_data.types.data();
        for (auto& it : declared_types)
        {
            *write_type++ = it.second;
        }


        std::sort(self_data.calls.begin(), self_data.calls.end(), sort_named<external_call_info>{});

        // Generate hash
        toolchain_version version = toolchain_version::current();
        self_data.hash = fnv::hash(&version, sizeof(toolchain_version));

        for (auto& call : self_data.calls)
        {
            self_data.hash = fnv::append(self_data.hash, call.return_type.name);
            self_data.hash = fnv::append(self_data.hash, call.return_type.size);
            self_data.hash = fnv::append(self_data.hash, call.return_type.pointer_depth);

            for (const auto& it : call.parameters)
            {
                self_data.hash = fnv::append(self_data.hash, it.name);
                self_data.hash = fnv::append(self_data.hash, it.size);
                self_data.hash = fnv::append(self_data.hash, it.pointer_depth);
            }
        }
    }
    library::~library()
    {

    }


    host_library::host_library(string_view path) : path(path), lib_handle(nullptr)
    {

    }
    host_library::~host_library()
    {
        if (lib_handle)
        {
            close();
        }
    }

    host_library::host_library(host_library&& other) noexcept : path(other.path), lib_handle(other.lib_handle)
    {
        other.lib_handle = nullptr;
    }
    host_library& host_library::operator=(host_library&& other) noexcept
    {
        if (&other != this)
        {
            if (lib_handle)
            {
                close();
            }

            lib_handle = other.lib_handle;
            other.lib_handle = nullptr;

            path = other.path;
        }
        return *this;
    }

    bool host_library::is_open() const noexcept
    {
        return lib_handle != nullptr;
    }

    bool host_library::open()
    {
        lib_handle = host::openlib(path.data());
        return lib_handle != nullptr;
    }
    void host_library::close()
    {
        host::closelib(lib_handle);
        lib_handle = nullptr;
    }

    method_handle host_library::get_proc(const char* name)
    {
        return host::loadsym(lib_handle, name);
    }
}