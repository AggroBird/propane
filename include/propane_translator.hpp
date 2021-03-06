#ifndef _HEADER_PROPANE_TRANSLATOR
#define _HEADER_PROPANE_TRANSLATOR

#include "propane_assembly.hpp"
#include "propane_intermediate.hpp"

namespace propane
{
    template<uint32_t language> class translator
    {
    public:
        translator() = delete;
    };

    // Experimental translator for generating C code out of Propane assemblies
    class translator_c
    {
    public:
        static void generate(const char* out_file, const class assembly& linked_assembly);
    };

    template<> class translator<language_c> : public translator_c {};

    // Experimental translator for Propane text out of Propane assemblies
    class translator_propane
    {
    public:
        static void generate(const char* out_file, const class assembly& linked_assembly);
    };

    template<> class translator<language_propane> : public translator_propane {};
}

#endif