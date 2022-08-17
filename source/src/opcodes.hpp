#ifndef _HEADER_OPCODES
#define _HEADER_OPCODES

#include "common.hpp"

namespace propane
{
    // Opcodes
    enum class opcode : uint8_t
    {
        noop,

        set,
        conv,

        ari_not,
        ari_neg,
        ari_mul,
        ari_div,
        ari_mod,
        ari_add,
        ari_sub,
        ari_lsh,
        ari_rsh,
        ari_and,
        ari_xor,
        ari_or,

        padd,
        psub,
        pdif,

        cmp,
        ceq,
        cne,
        cgt,
        cge,
        clt,
        cle,
        cze,
        cnz,

        br,
        beq,
        bne,
        bgt,
        bge,
        blt,
        ble,
        bze,
        bnz,

        sw,

        call,
        callv,
        ret,
        retv,

        dump,
    };

    enum class subcode : uint8_t { invalid = 0xFF };

    constexpr opcode operator+(opcode lhs, opcode rhs) noexcept
    {
        return opcode(static_cast<uint32_t>(lhs) + static_cast<uint32_t>(rhs));
    }
    constexpr opcode operator-(opcode lhs, opcode rhs) noexcept
    {
        return opcode(static_cast<uint32_t>(lhs) - static_cast<uint32_t>(rhs));
    }
}

#endif