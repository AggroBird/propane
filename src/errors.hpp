#ifndef _HEADER_ERRORS
#define _HEADER_ERRORS

#include "common.hpp"

enum class ERRC : uint32_t
{
    // Generator errors
    GNR_INVALID_ASSEMBLY = 0x1000,
    GNR_INCOMPATIBLE_ASSEMBLY = 0x1001,
    GNR_ENTRYPOINT_NOT_FOUND = 0x1002,
    GNR_FILE_EXCEPTION = 0x1003,
    GNR_INVALID_IDENTIFIER = 0x1100,
    GNR_PARAMETER_OVERFLOW = 0x1101,
    GNR_INITIALIZER_OVERFLOW = 0x1102,
    GNR_INDEX_OUT_OF_RANGE = 0x1103,
    GNR_ARRAY_LENGTH_ZERO = 0x1104,
    GNR_IDENTIFIER_TYPE_MISMATCH = 0x1200,
    GNR_INVALID_VOID_TYPE = 0x1201,
    GNR_TYPE_REDECLARATION = 0x1202,
    GNR_METHOD_REDECLARATION = 0x1203,
    GNR_GLOBAL_REDECLARATION = 0x1204,
    GNR_FIELD_REDECLARATION = 0x1205,
    //GNR_STACK_REDECLARATION = 0x1206,
    GNR_LABEL_REDECLARATION = 0x1207,
    GNR_LABEL_UNDEFINED = 0x1208,
    GNR_INVALID_RET_VAL = 0x1300,
    GNR_STACK_OUT_OF_RANGE = 0x1301,
    GNR_PARAM_OUT_OF_RANGE = 0x1302,
    GNR_INVALID_CONSTANT = 0x1303,
    GNR_MISSING_RET_VAL = 0x1304,
    // Parser errors
    PRS_FILE_EXCEPTION = 0x2000,
    PRS_UNEXPECTED_EXPRESSION = 0x2100,
    PRS_UNEXPECTED_CHARACTER = 0x2101,
    PRS_UNEXPECTED_EOF = 0x2102,
    PRS_UNTERMINATED_COMMENT = 0x2103,
    PRS_UNTERMINATED_CHARACTER = 0x2104,
    PRS_UNEXPECTED_END = 0x2105,
    PRS_LITERAL_PARSE_FAILURE = 0x2106,
    PRS_ARRAY_SIZE_OVERFLOW = 0x2107,
    PRS_STACK_IDX_OVERFLOW = 0x2108,
    PRS_UNDEFINED_STACK_IDX = 0x2109,
    PRS_DUPLICATE_STACK_IDX = 0x210A,
    PRS_UNDEFINED_PARAM_IDX = 0x210B,
    PRS_DUPLICATE_PARAM_IDX = 0x210C,
    PRS_DUPLICATE_STACK_NAME = 0x210D,
    PRS_UNEXPECTED_LITERAL = 0x210E,
    // Merger errors
    MRG_INVALID_INTERMEDIATE = 0x3000,
    MRG_INCOMPATIBLE_INTERMEDIATE = 0x3001,
    MRG_INDEX_OUT_OF_RANGE = 0x3100,
    MRG_TYPE_REDEFINITION = 0x3101,
    MRG_METHOD_REDEFINITION = 0x3102,
    MRG_GLOBAL_REDEFINITION = 0x3103,
    MRG_IDENTIFIER_TYPE_MISMATCH = 0x3104,
    // Linker errors
    LNK_INVALID_INTERMEDIATE = 0x4000,
    LNK_INCOMPATIBLE_INTERMEDIATE = 0x4001,
    LNK_RECURSIVE_TYPE_DEFINITION = 0x4100,
    LNK_UNDEFINED_TYPE = 0x4101,
    LNK_UNDEFINED_METHOD = 0x4102,
    LNK_UNDEFINED_GLOBAL = 0x4103,
    LNK_TYPE_SIZE_ZERO = 0x4104,
    LNK_UNINITIALIZED_METHOD_PTR = 0x4200,
    LNK_UNDEFINED_METHOD_INITIALIZER = 0x4201,
    LNK_INVALID_METHOD_INITIALIZER = 0x4202,
    LNK_GLOBAL_INITIALIZER_OVERFLOW = 0x4203,
    LNK_UNDEFINED_TYPE_FIELD = 0x4204,
    LNK_INVALID_IMPLICIT_CONVERSION = 0x4300,
    LNK_INVALID_EXPLICIT_CONVERSION = 0x4301,
    LNK_INVALID_ARITHMETIC_EXPRESSION = 0x4302,
    LNK_INVALID_COMPARISON_EXPRESSION = 0x4303,
    LNK_INVALID_POINTER_EXPRESSION = 0x4304,
    LNK_INVALID_PTR_OFFSET_EXPRESSION = 0x4305,
    LNK_INVALID_SWITCH_TYPE = 0x4306,
    LNK_FUNCTION_ARGUMENT_COUNT_MISMATCH = 0x4307,
    LNK_NON_SIGNATURE_TYPE_INVOKE = 0x4308,
    LNK_INVALID_RETURN_ADDRESS = 0x4309,
    LNK_ARRAY_INDEX_OUT_OF_RANGE = 0x430A,
    LNK_INVALID_OFFSET_MODIFIER = 0x430B,
    LNK_FIELD_PARENT_TYPE_MISMATCH = 0x430C,
    LNK_INVALID_POINTER_DEREFERENCE = 0x430D,
    LNK_ABSTRACT_POINTER_DEREFERENCE = 0x430E,
    LNK_INVALID_FIELD_DEREFERENCE = 0x430F,
    // Runtime errors
    RTM_INVALID_ASSEMBLY = 0x5000,
    RTM_INCOMPATIBLE_ASSEMBLY = 0x5001,
    RTM_ENTRYPOINT_NOT_FOUND = 0x5002,
    RTM_STACK_ALLOCATION_FAILURE = 0x5003,
    RTM_STACK_OVERFLOW = 0x5004,
    RTM_CALLSTACK_LIMIT_REACHED = 0x5005,
};

inline uint32_t errc_to_uint(ERRC errc) noexcept
{
    return uint32_t(errc);
}
template<typename value_t> inline bool validate_expression(value_t expr) noexcept
{
    static_assert(std::is_same_v<value_t, bool>, "Expression does not yield a boolean");
    return false;
}
template<> inline bool validate_expression<bool>(bool expr) noexcept
{
    return expr;
}

#if 1
#define ENSURE(errc, expr, excep, ...)                      \
{                                                           \
    if(!validate_expression(expr))                          \
    {                                                       \
        throw excep(                                        \
            errc_to_uint(errc),                             \
            propane::format(__VA_ARGS__).data());           \
        abort();                                            \
    }                                                       \
}
#define ENSURE_WITH_META(errc, meta, expr, excep, ...)      \
{                                                           \
    if(!validate_expression(expr))                          \
    {                                                       \
        const auto meta_copy = meta;                        \
        throw excep(                                        \
            errc_to_uint(errc),                             \
            propane::format(__VA_ARGS__).data(),            \
            meta_copy.file_name,                            \
            meta_copy.line_number);                         \
        abort();                                            \
    }                                                       \
}
#define ASSERT(expr, fmt, ...)                                                      \
{                                                                                   \
    if(!(expr))                                                                     \
    {                                                                               \
        const std::string str = propane::format("%:%, assertion failed: " fmt,      \
            propane::strip_filepath(__FILE__), __LINE__, 0);              \
        fprintf(stderr, "%s", str.data());                                          \
        abort();                                                                    \
    }                                                                               \
}
#else
#define ENSURE(...) ((void)0);
#define ENSURE_WITH_META(...) ((void)0);
#define ASSERT(...) ((void)0);
#endif

#endif