#ifndef _HEADER_LIBRARY
#define _HEADER_LIBRARY

#include "propane_library.hpp"
#include "common.hpp"

namespace propane
{
    class external_call_info
    {
    public:
        external_call_info(string_view name = string_view()) :
            name(name) {}

        string name;
        type_idx return_type = type_idx::invalid;
        std::span<const stackvar> parameters;
        size_t parameters_size = 0;

        external_call::forward_method forward = nullptr;
        void* handle = nullptr;
        index_t index = 0;
        name_idx library = name_idx::invalid;
    };

    class library_data
    {
    public:
        MOVABLE_CLASS_DEFAULT(library_data, string_view path = string_view()) :
            path(path) {}

        string path;
        block<external_call_info> calls;
    };

    class host_library final
    {
    public:
        host_library(string_view path);
        ~host_library();

        host_library(host_library&& other) noexcept;
        host_library& operator=(host_library&& other) noexcept;

        host_library(const host_library&) = delete;
        host_library& operator=(const host_library&) = delete;

        bool is_open() const noexcept;

        bool open();
        void close();

        void* get_proc(const char* name);

    private:
        string_view path;
        void* handle;
    };
}

#endif