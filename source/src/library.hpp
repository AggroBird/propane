#ifndef _HEADER_LIBRARY
#define _HEADER_LIBRARY

#include "propane_library.hpp"
#include "common.hpp"

namespace propane
{
    template<typename value_t> struct sort_named
    {
        inline constexpr bool operator()(const value_t& lhs, const value_t& rhs) const noexcept
        {
            return lhs.name < rhs.name;
        }
    };

    class external_call_info
    {
    public:
        external_call_info(string_view name = string_view()) :
            name(name) {}

        string name;
        native::typedecl return_type;
        span<const native::parameter> parameters;
        size_t parameters_size = 0;

        external_call::forward_method_handle forward = nullptr;
        method_handle handle = nullptr;
    };

    class library_data
    {
    public:
        MOVABLE_CLASS_DEFAULT(library_data, string_view path, bool preload_symbols) :
            path(path),
            preload_symbols(preload_symbols) {}

        string path;
        bool preload_symbols;
        block<external_call_info> calls;
        block<native::typedecl> types;
        size_t hash = 0;
    };
    constexpr size_t library_data_handle_size = approximate_handle_size(sizeof(library_data));

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

        method_handle get_proc(const char* name);

    private:
        string path;
        void* lib_handle;
    };
}

#endif