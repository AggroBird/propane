#ifndef _HEADER_INTERNAL
#define _HEADER_INTERNAL

#include "runtime.hpp"

namespace propane
{
    struct internal_call_info
    {
        method_idx index = method_idx::invalid;
        string_view name;
        type_idx return_type = type_idx::invalid;
        block<stackvar> parameters;
        size_t parameters_size = 0;
    };

    const internal_call_info& get_internal_call(size_t idx);
    size_t internal_call_count();
    size_t internal_call_hash();
}

#endif