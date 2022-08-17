#include "utility.hpp"
#include "errors.hpp"

#define OPCODE_STR(val) case opcode::val: return #val
#define OPCODE_ARI_STR(val) case opcode::ari_##val: return #val

namespace propane
{
    string_view opcode_str(opcode op)
    {
        switch (op)
        {
            OPCODE_STR(noop);

            OPCODE_STR(set);
            OPCODE_STR(conv);

            OPCODE_ARI_STR(not);
            OPCODE_ARI_STR(neg);
            OPCODE_ARI_STR(mul);
            OPCODE_ARI_STR(div);
            OPCODE_ARI_STR(mod);
            OPCODE_ARI_STR(add);
            OPCODE_ARI_STR(sub);
            OPCODE_ARI_STR(lsh);
            OPCODE_ARI_STR(rsh);
            OPCODE_ARI_STR(and);
            OPCODE_ARI_STR(xor);
            OPCODE_ARI_STR(or );

            OPCODE_STR(padd);
            OPCODE_STR(psub);
            OPCODE_STR(pdif);

            OPCODE_STR(cmp);
            OPCODE_STR(ceq);
            OPCODE_STR(cne);
            OPCODE_STR(cgt);
            OPCODE_STR(cge);
            OPCODE_STR(clt);
            OPCODE_STR(cle);
            OPCODE_STR(cze);
            OPCODE_STR(cnz);

            OPCODE_STR(br);
            OPCODE_STR(beq);
            OPCODE_STR(bne);
            OPCODE_STR(bgt);
            OPCODE_STR(bge);
            OPCODE_STR(blt);
            OPCODE_STR(ble);
            OPCODE_STR(bze);
            OPCODE_STR(bnz);

            OPCODE_STR(sw);

            OPCODE_STR(call);
            OPCODE_STR(callv);
            OPCODE_STR(ret);
            OPCODE_STR(retv);

            OPCODE_STR(dump);

            default: ASSERT(false, "Unknown opcode"); return string_view();
        }
    }
}

std::ostream& operator<<(std::ostream& stream, const propane::lookup_type& type)
{
    switch (type)
    {
        case propane::lookup_type::type: return stream << "type";
        case propane::lookup_type::method: return stream << "method";
        case propane::lookup_type::global: return stream << "global";
        case propane::lookup_type::constant: return stream << "constant";
        case propane::lookup_type::identifier: return stream << "identifier";
        default: stream << "<UNKNOWN LOOKUP TYPE>"; break;
    }
    return stream;
}

std::ostream& operator<<(std::ostream& stream, const propane::file_meta& meta)
{
    return stream << (meta.file_name.empty() ? "<unknown>" : meta.file_name) << ':' << meta.line_number;
}

std::ostream& operator<<(std::ostream& stream, const propane::opcode& op)
{
    return stream << propane::opcode_str(op);
}

std::ostream& operator<<(std::ostream& stream, const propane::toolchain_version& op)
{
    stream
        << op.major() << '.'
        << op.minor() << '.'
        << op.changelist() << '-';
    switch (op.endianness())
    {
        case propane::platform_endianness::little: stream << "LE"; break;
        case propane::platform_endianness::big: stream << "BE"; break;
        case propane::platform_endianness::little_word: stream << "LW"; break;
        case propane::platform_endianness::big_word: stream << "BW"; break;
        default: stream << "??"; break;
    }
    switch (op.architecture())
    {
        case propane::platform_architecture::x32: stream << "32"; break;
        case propane::platform_architecture::x64: stream << "64"; break;
        default: stream << "??"; break;
    }
    return stream;
}

std::ostream& operator<<(std::ostream& stream, const propane::parse_result<propane::literal_t>& id)
{
    switch (id.type)
    {
        case propane::type_idx::i8: stream << static_cast<int32_t>(id.value.as_i8); break;
        case propane::type_idx::u8: stream << static_cast<int32_t>(id.value.as_u8); break;
        case propane::type_idx::i16: stream << id.value.as_i16; break;
        case propane::type_idx::u16: stream << id.value.as_u16; break;
        case propane::type_idx::i32: stream << id.value.as_i32; break;
        case propane::type_idx::u32: stream << id.value.as_u32; break;
        case propane::type_idx::i64: stream << id.value.as_i64; break;
        case propane::type_idx::u64: stream << id.value.as_u64; break;
        case propane::type_idx::f32: stream << id.value.as_f32; break;
        case propane::type_idx::f64: stream << id.value.as_f64; break;
        case propane::type_idx::vptr: stream << id.value.as_vptr; break;
        default: stream << "???"; break;
    }
    return stream;
}