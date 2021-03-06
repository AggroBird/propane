#include "library.hpp"
#include "errors.hpp"
#include "utility.hpp"

namespace propane
{
    external_call::external_call(std::string_view name) :
        name(name) {}

    library::library(string_view path, bool preload_symbols, span<const external_call> calls) : handle(path, preload_symbols)
    {
        auto& data = self();

        data.calls = block<external_call_info>(calls.size());
        external_call_info* write = data.calls.data();
        for (auto& call : calls)
        {
            ASSERT(is_identifier(call.name), "Invalid name");

            for (auto& type : call.parameters)
            {
                ASSERT(is_identifier(type.type), "Invalid name");
            }

            external_call_info info(call.name);
            info.return_type = call.return_type;
            info.parameters = call.parameters;
            info.parameters_size = call.parameters_size;
            info.forward = call.forward;
            info.handle = call.handle;
            *write++ = std::move(info);
        }

        std::sort(data.calls.begin(), data.calls.end(), sort_named<external_call_info>{});

        // Generate hash
        toolchain_version version = toolchain_version::current();
        data.hash = fnv::hash(&version, sizeof(toolchain_version));

        for (auto& call : data.calls)
        {
            data.hash = fnv::append(data.hash, call.return_type.type);
            data.hash = fnv::append(data.hash, call.return_type.size);
            data.hash = fnv::append(data.hash, call.return_type.pointer);

            for (const auto& it : call.parameters)
            {
                data.hash = fnv::append(data.hash, it.type);
                data.hash = fnv::append(data.hash, it.size);
                data.hash = fnv::append(data.hash, it.pointer);
            }
        }
    }
    library::~library()
    {

    }


    host_library::host_library(string_view path) : path(path), handle(nullptr)
    {

    }
    host_library::~host_library()
    {
        if (handle)
        {
            close();
        }
    }

    host_library::host_library(host_library&& other) noexcept : path(other.path), handle(other.handle)
    {
        other.handle = nullptr;
    }
    host_library& host_library::operator=(host_library&& other) noexcept
    {
        if (&other != this)
        {
            if (handle)
            {
                close();
            }

            handle = other.handle;
            other.handle = nullptr;

            path = other.path;
        }
        return *this;
    }

    bool host_library::is_open() const noexcept
    {
        return handle != nullptr;
    }

    bool host_library::open()
    {
        handle = host::openlib(path.data());
        return handle != nullptr;
    }
    void host_library::close()
    {
        host::closelib(handle);
        handle = nullptr;
    }

    void* host_library::get_proc(const char* name)
    {
        return host::loadsym(handle, name);
    }
}