#include "library.hpp"
#include "errors.hpp"
#include "utility.hpp"

namespace propane
{
    external_call::external_call(std::string_view name) :
        name(name) {}

    library::library(string_view path, span<const external_call> calls) : handle(path)
    {
        auto& data = self();

        size_t idx = 0;
        data.calls = block<external_call_info>(calls.size());
        for (auto& call : calls)
        {
            ASSERT(is_identifier(call.name), "Invalid name");

            external_call_info info(call.name);
            info.return_type = call.return_type;
            info.parameters = call.parameters;
            info.parameters_size = call.parameters_size;
            info.forward = call.forward;
            info.handle = call.handle;
            data.calls[idx++] = std::move(info);
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