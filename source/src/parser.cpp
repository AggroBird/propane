#include "propane_parser.hpp"
#include "propane_generator.hpp"
#include "database.hpp"
#include "utility.hpp"
#include "errors.hpp"
#include "propane_literals.hpp"
#include "parser_tokens.hpp"

#include <charconv>
#include <system_error>

#define VALIDATE(errc, expr, ...) ENSURE_WITH_META(errc, this->get_meta(), expr, propane::generator_exception, __VA_ARGS__)

#define VALIDATE_FILE_OPEN(expr, file_path) VALIDATE(ERRC::PRS_FILE_EXCEPTION, expr, \
    "Failed to open file: \"%\"", file_path)
#define UNEXPECTED_EXPRESSION(expr, expression) VALIDATE(ERRC::PRS_UNEXPECTED_EXPRESSION, expr, \
    "Unexpected expression: '%'", expression)
#define UNEXPECTED_CHARACTER(expr, character) VALIDATE(ERRC::PRS_UNEXPECTED_CHARACTER, expr, \
    "Unexpected '%' character", character)
#define UNEXPECTED_EOF(expr) VALIDATE(ERRC::PRS_UNEXPECTED_EOF, expr, \
    "Unexpected end of file")
#define UNTERMINATED_COMMENT(expr) VALIDATE(ERRC::PRS_UNTERMINATED_COMMENT, expr, \
    "Comment unclosed at end of file")
#define UNTERMINATED_CHARACTER(expr, character) VALIDATE(ERRC::PRS_UNTERMINATED_CHARACTER, expr, \
    "Unterminated '%' character", character)
#define UNEXPECTED_END(expr) VALIDATE(ERRC::PRS_UNEXPECTED_END, expr, \
    "Unexpected end of scope")
#define LITERAL_PARSE_FAILURE(expr, expression) VALIDATE(ERRC::PRS_LITERAL_PARSE_FAILURE, expr, \
    "Failed to parse literal: '%'", expression)
#define VALIDATE_ARRAY_SIZE(num) VALIDATE(ERRC::PRS_ARRAY_SIZE_OVERFLOW, propane::check_size_range(num), \
    "Array size exceeds supported maximum value")
#define VALIDATE_STACK_INDEX(num) VALIDATE(ERRC::PRS_STACK_IDX_OVERFLOW, uint64_t(num) < uint64_t(address_header_constants::index_max), \
    "Index exceeds supported maximum value")
#define UNDEFINED_STACK_IDX(expr, num) VALIDATE(ERRC::PRS_UNDEFINED_STACK_IDX, expr, \
    "Undefined stack index: '%'", num)
#define DUPLICATE_STACK_IDX(expr, num) VALIDATE(ERRC::PRS_DUPLICATE_STACK_IDX, expr, \
    "Stack index '%' has already been defined", num)
#define UNDEFINED_PARAM_IDX(expr, num) VALIDATE(ERRC::PRS_UNDEFINED_PARAM_IDX, expr, \
    "Undefined parameter index: '%'", num)
#define DUPLICATE_PARAM_IDX(expr, num) VALIDATE(ERRC::PRS_DUPLICATE_PARAM_IDX, expr, \
    "Parameter index '%' has already been defined", num)
#define DUPLICATE_STACK_NAME(expr, name) VALIDATE(ERRC::PRS_DUPLICATE_STACK_NAME, expr, \
    "Variable '%' has already been defined", name)
#define UNEXPECTED_LITERAL(expr) VALIDATE(ERRC::PRS_UNEXPECTED_LITERAL, expr, \
    "Literal is not valid here")

namespace propane
{
    struct token
    {
    public:
        token(token_type type, string_view str, index_t line_num) : type(type), str(str), line_num(line_num)
        {

        }

        const token_type type;
        const string_view str;
        const index_t line_num;

        inline operator string_view() const
        {
            return str;
        }
    };

    enum class definition_type
    {
        none = 0,
        object,
        method,
    };

    enum class comment_type
    {
        none = 0,
        single,
        multi,
    };

    // Experimental implementation of propane generator
    class parser_impl final : public generator
    {
    public:
        NOCOPY_CLASS_DEFAULT(parser_impl) = delete;

        parser_impl(const char* file_path) :
            generator(strip_filepath(file_path))
        {
            block<char> file_text;

            // Read file (and close thereafter)
            {
                ifstream file(file_path, std::ios::binary);
                VALIDATE_FILE_OPEN(file.is_open(), file_path);
                file.seekg(0, file.end);
                const std::streamsize file_size = file.tellg();
                file.seekg(0, file.beg);
                // Add one extra newline
                file_text = block<char>(size_t(file_size) + 1);
                file_text[file_size] = '\n';
                file.read((char*)file_text.data(), file_size);
            }

            const char* beg = file_text.data();
            const char* end = beg + file_text.size();
            const char* ptr = beg;
            line_num = 1;
            set_line_number(line_num);
            comment_type comment = comment_type::none;
            while (ptr < end)
            {
                const char c = *ptr++;
                if (c == '\n')
                {
                    set_line_number(++line_num);
                    beg = ptr;
                    if (comment == comment_type::single)
                    {
                        comment = comment_type::none;
                    }
                    continue;
                }

                if (comment != comment_type::none)
                {
                    if (c == '*' && *ptr == '/')
                    {
                        ptr++;
                        comment = comment_type::none;
                    }
                    continue;
                }

                switch (c)
                {
                    case ' ':
                    case '\r':
                    case '\t':
                    case '\v':
                        break;

                    case '/':
                    {
                        const char n = *ptr++;
                        switch (n)
                        {
                            case '/': comment = comment_type::single; break;
                            case '*': comment = comment_type::multi; break;
                            default: UNEXPECTED_CHARACTER(false, c); break;
                        }
                        break;
                    }
                    break;

                    default:
                    {
                        if (is_identifier(c, true))
                        {
                            for (; ptr < end;)
                            {
                                const char n = *ptr;
                                if (!is_identifier(n, false))
                                {
                                    const auto len = ptr - beg;
                                    if (len > 0)
                                    {
                                        const string_view str = string_view(beg, len);
                                        auto lookup_result = token_string_lookup_table.try_find_token(str);
                                        if (lookup_result.type != token_type::invalid)
                                        {
                                            // Treat as keyword
                                            add_token(lookup_result.type, lookup_result.str);
                                        }
                                        else
                                        {
                                            // Treat as identifier
                                            add_token(token_type::identifier, str);
                                        }
                                    }
                                    break;
                                }
                                ptr++;
                            }
                        }
                        else if (is_literal(c) && *ptr != '>')
                        {
                            for (; ptr < end;)
                            {
                                const char n = *ptr;
                                if (!((n >= '0' && n <= '9') || (n >= 'a' && n <= 'z') || (n >= 'A' && n <= 'Z') || n == '.' || n == '-'))
                                {
                                    const auto len = ptr - beg;
                                    if (len > 0)
                                    {
                                        // Literal
                                        const string_view str = string_view(beg, len);
                                        add_token(token_type::literal, str);
                                        break;
                                    }
                                }
                                ptr++;
                            }
                        }
                        else
                        {
                            switch (c)
                            {
                                case '{': add_token(token_type::lbrace, ptr, 1); break;
                                case '}': add_token(token_type::rbrace, ptr, 1); break;
                                case '[': add_token(token_type::lbracket, ptr, 1); break;
                                case ']': add_token(token_type::rbracket, ptr, 1); break;
                                case '(': add_token(token_type::lparen, ptr, 1); break;
                                case ')': add_token(token_type::rparen, ptr, 1); break;
                                case '-':
                                {
                                    const char n = *ptr++;
                                    UNEXPECTED_CHARACTER(n == '>', c);
                                    add_token(token_type::deref, ptr, 2);
                                    break;
                                }
                                break;
                                case '*': add_token(token_type::asterisk, ptr, 1); break;
                                case '&': add_token(token_type::ampersand, ptr, 1); break;
                                case '!': add_token(token_type::exclamation, ptr, 1); break;
                                case '^': add_token(token_type::circumflex, ptr, 1); break;
                                case ':': add_token(token_type::colon, ptr, 1); break;
                                case ',': add_token(token_type::comma, ptr, 1); break;
                                case '.': add_token(token_type::period, ptr, 1); break;
                                default: UNEXPECTED_CHARACTER(false, c); break;
                            }
                        }
                    }
                    break;
                }
                beg = ptr;
            }

            UNEXPECTED_EOF(current_scope == definition_type::none);
            UNTERMINATED_COMMENT(comment != comment_type::multi);

            if (tokens.size() > 0) evaluate();
        }

    private:
        void add_token(token_type type, const char* c, size_t len)
        {
            tokens.push_back(token(type, string_view(c - len, len), line_num));
        }
        void add_token(token_type type, string_view str)
        {
            tokens.push_back(token(type, str, line_num));
        }

        void evaluate()
        {
            tokens.push_back(token(token_type::eof, "EOF", line_num));

            const token* ptr = tokens.data();
            const token* const end = ptr + (tokens.size() - 1);
            while (ptr < end)
            {
                const token& t = *ptr;
                set_line_number(t.line_num);

                switch (current_scope)
                {
                    case definition_type::none:
                    {
                        ptr++;

                        switch (t.type)
                        {
                            case token_type::kw_global: parse_globals(ptr, false); continue;
                            case token_type::kw_constant: parse_globals(ptr, true); continue;
                            case token_type::kw_method: begin_method(ptr); continue;
                            case token_type::kw_struct: begin_struct(ptr); continue;
                            case token_type::kw_union: begin_union(ptr); continue;
                            case token_type::kw_end: UNEXPECTED_END(false);
                        }
                    }
                    break;

                    case definition_type::object:
                    {
                        switch (t.type)
                        {
                            case token_type::identifier: parse_field(ptr); continue;
                            case token_type::kw_end: ptr++; end_object(); continue;
                        }
                    }
                    break;

                    case definition_type::method:
                    {
                        ptr++;

                        switch (t.type)
                        {
                            case token_type::kw_stack: parse_stack(ptr); continue;

                            case token_type::op_noop: write_noop(); continue;
                            case token_type::op_set: write_set(ptr); continue;
                            case token_type::op_conv: write_conv(ptr); continue;
                            case token_type::op_not: write_not(ptr); continue;
                            case token_type::op_neg: write_neg(ptr); continue;
                            case token_type::op_mul: write_mul(ptr); continue;
                            case token_type::op_div: write_div(ptr); continue;
                            case token_type::op_mod: write_mod(ptr); continue;
                            case token_type::op_add: write_add(ptr); continue;
                            case token_type::op_sub: write_sub(ptr); continue;
                            case token_type::op_lsh: write_lsh(ptr); continue;
                            case token_type::op_rsh: write_rsh(ptr); continue;
                            case token_type::op_and: write_and(ptr); continue;
                            case token_type::op_xor: write_xor(ptr); continue;
                            case token_type::op_or: write_or(ptr); continue;
                            case token_type::op_padd: write_padd(ptr); continue;
                            case token_type::op_psub: write_psub(ptr); continue;
                            case token_type::op_pdif: write_pdif(ptr); continue;
                            case token_type::op_cmp: write_cmp(ptr); continue;
                            case token_type::op_ceq: write_ceq(ptr); continue;
                            case token_type::op_cne: write_cne(ptr); continue;
                            case token_type::op_cgt: write_cgt(ptr); continue;
                            case token_type::op_cge: write_cge(ptr); continue;
                            case token_type::op_clt: write_clt(ptr); continue;
                            case token_type::op_cle: write_cle(ptr); continue;
                            case token_type::op_cze: write_cze(ptr); continue;
                            case token_type::op_cnz: write_cnz(ptr); continue;
                            case token_type::op_br: write_br(ptr); continue;
                            case token_type::op_beq: write_beq(ptr); continue;
                            case token_type::op_bne: write_bne(ptr); continue;
                            case token_type::op_bgt: write_bgt(ptr); continue;
                            case token_type::op_bge: write_bge(ptr); continue;
                            case token_type::op_blt: write_blt(ptr); continue;
                            case token_type::op_ble: write_ble(ptr); continue;
                            case token_type::op_bze: write_bze(ptr); continue;
                            case token_type::op_bnz: write_bnz(ptr); continue;
                            case token_type::op_sw: write_sw(ptr); continue;
                            case token_type::op_call: write_call(ptr); continue;
                            case token_type::op_callv: write_callv(ptr); continue;
                            case token_type::op_ret: write_ret(); continue;
                            case token_type::op_retv: write_retv(ptr); continue;
                            case token_type::op_dump: write_dump(ptr); continue;

                            case token_type::kw_end: end_method(); continue;
                        }
                    }
                    break;
                }

                UNEXPECTED_EXPRESSION(false, t.str);
            }
        }


        void begin_struct(const token*& ptr)
        {
            UNEXPECTED_EXPRESSION(ptr->type == token_type::identifier, ptr->str);

            current_scope = definition_type::object;

            set_line_number(ptr->line_num);
            current_type = &define_type((ptr++)->str, false);
        }
        void begin_union(const token*& ptr)
        {
            UNEXPECTED_EXPRESSION(ptr->type == token_type::identifier, ptr->str);

            current_scope = definition_type::object;

            set_line_number(ptr->line_num);
            current_type = &define_type((ptr++)->str, true);
        }
        void parse_field(const token*& ptr)
        {
            type_idx field_type = parse_typename(ptr);

            UNEXPECTED_EXPRESSION(ptr->type == token_type::identifier, ptr->str);
            set_line_number(ptr->line_num);
            current_type->declare_field(field_type, (ptr++)->str);
        }

        void begin_method(const token*& ptr)
        {
            UNEXPECTED_EXPRESSION(ptr->type == token_type::identifier, ptr->str);
            method_idx method_index = declare_method((ptr++)->str);

            // Parse return type
            type_idx method_return_type = type_idx::voidtype;
            if (ptr->type == token_type::kw_returns)
            {
                ptr++;
                method_return_type = parse_typename(ptr);
            }

            // Parse parameters
            parameters.clear();
            if (ptr->type == token_type::kw_parameters)
            {
                ptr++;

            parse_next_parameter:
                switch (ptr->type)
                {
                    case token_type::identifier:
                    case token_type::literal:
                        parameters.push_back(parse_parameter(ptr, true));
                        goto parse_next_parameter;

                    case token_type::kw_end: ptr++; break;

                    default: UNEXPECTED_EXPRESSION(false, ptr->str);
                }
            }

            current_scope = definition_type::method;

            set_line_number(ptr->line_num);
            current_method = &define_method(method_index, make_signature(method_return_type, parameters));
        }

        void parse_stack(const token*& ptr)
        {
            set_line_number(ptr->line_num);
            parameters.clear();

        parse_next_parameter:
            switch (ptr->type)
            {
                case token_type::identifier:
                case token_type::literal:
                    parameters.push_back(parse_parameter(ptr, false));
                    goto parse_next_parameter;

                case token_type::kw_end: ptr++; break;

                default: UNEXPECTED_EXPRESSION(false, ptr->str);
            }

            if (parameters.size() > 0)
            {
                current_method->push(parameters);
            }
        }

        vector<constant> constant_buffer;
        void parse_globals(const token*& ptr, bool is_constant)
        {
        parse_next_global:
            switch (ptr->type)
            {
                case token_type::identifier:
                {
                    const type_idx global_type = parse_typename(ptr);
                    set_line_number(ptr->line_num);
                    const string_view global_name = (ptr++)->str;
                    constant_buffer.clear();
                    if (ptr->type == token_type::kw_init)
                    {
                        ptr++;
                    parse_next_constant:
                        switch (ptr->type)
                        {
                            default: constant_buffer.push_back(parse_constant(ptr)); goto parse_next_constant;
                            case token_type::kw_end: ptr++; break;
                        }
                    }
                    define_global(make_identifier(global_name), is_constant, global_type, constant_buffer);
                    goto parse_next_global;
                }

                case token_type::kw_end: ptr++; break;
            }
        }


        void end_object()
        {
            current_type->finalize();
            current_type = nullptr;

            current_scope = definition_type::none;
        }
        void end_method()
        {
            stackvar_lookup.clear();
            parameter_lookup.clear();

            current_method->finalize();
            current_method = nullptr;

            current_scope = definition_type::none;
        }


        void write_noop()
        {
            current_method->write_noop();
        }

        void write_set(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_set(lhs, rhs);
        }
        void write_conv(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_conv(lhs, rhs);
        }

        void write_not(const token*& ptr)
        {
            const address addr = parse_address(ptr);
            current_method->write_not(addr);
        }
        void write_neg(const token*& ptr)
        {
            const address addr = parse_address(ptr);
            current_method->write_neg(addr);
        }
        void write_mul(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_mul(lhs, rhs);
        }
        void write_div(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_div(lhs, rhs);
        }
        void write_mod(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_mod(lhs, rhs);
        }
        void write_add(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_add(lhs, rhs);
        }
        void write_sub(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_sub(lhs, rhs);
        }
        void write_lsh(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_lsh(lhs, rhs);
        }
        void write_rsh(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_rsh(lhs, rhs);
        }
        void write_and(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_and(lhs, rhs);
        }
        void write_xor(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_xor(lhs, rhs);
        }
        void write_or(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_or(lhs, rhs);
        }

        void write_padd(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_padd(lhs, rhs);
        }
        void write_psub(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_psub(lhs, rhs);
        }
        void write_pdif(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_pdif(lhs, rhs);
        }

        void write_cmp(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_cmp(lhs, rhs);
        }
        void write_ceq(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_ceq(lhs, rhs);
        }
        void write_cne(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_cne(lhs, rhs);
        }
        void write_cgt(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_cgt(lhs, rhs);
        }
        void write_cge(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_cge(lhs, rhs);
        }
        void write_clt(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_clt(lhs, rhs);
        }
        void write_cle(const token*& ptr)
        {
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_cle(lhs, rhs);
        }
        void write_cze(const token*& ptr)
        {
            const address addr = parse_address(ptr);
            current_method->write_cze(addr);
        }
        void write_cnz(const token*& ptr)
        {
            const address addr = parse_address(ptr);
            current_method->write_cnz(addr);
        }


        label_idx parse_label(const token*& ptr)
        {
            UNEXPECTED_EXPRESSION(ptr->type == token_type::identifier, ptr->str);
            return current_method->declare_label((ptr++)->str);
        }

        void write_br(const token*& ptr)
        {
            const label_idx label = parse_label(ptr);
            current_method->write_br(label);
        }
        void write_beq(const token*& ptr)
        {
            const label_idx label = parse_label(ptr);
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_beq(label, lhs, rhs);
        }
        void write_bne(const token*& ptr)
        {
            const label_idx label = parse_label(ptr);
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_bne(label, lhs, rhs);
        }
        void write_bgt(const token*& ptr)
        {
            const label_idx label = parse_label(ptr);
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_bgt(label, lhs, rhs);
        }
        void write_bge(const token*& ptr)
        {
            const label_idx label = parse_label(ptr);
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_bge(label, lhs, rhs);
        }
        void write_blt(const token*& ptr)
        {
            const label_idx label = parse_label(ptr);
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_blt(label, lhs, rhs);
        }
        void write_ble(const token*& ptr)
        {
            const label_idx label = parse_label(ptr);
            const address lhs = parse_address(ptr);
            const address rhs = parse_address(ptr);
            current_method->write_ble(label, lhs, rhs);
        }
        void write_bze(const token*& ptr)
        {
            const label_idx label = parse_label(ptr);
            current_method->write_bze(label, parse_address(ptr));
        }
        void write_bnz(const token*& ptr)
        {
            const label_idx label = parse_label(ptr);
            current_method->write_bnz(label, parse_address(ptr));
        }

        vector<label_idx> label_buffer;
        void write_sw(const token*& ptr)
        {
            const address addr = parse_address(ptr);

            label_buffer.clear();
            while (ptr->type == token_type::identifier)
            {
                label_buffer.push_back(current_method->declare_label(ptr->str));
                ptr++;
            }

            current_method->write_sw(addr, label_buffer);
        }

        vector<address> arg_buffer;
        void write_call(const token*& ptr)
        {
            UNEXPECTED_CHARACTER(ptr->type == token_type::identifier, ptr->str);
            const string_view method = (ptr++)->str;

            arg_buffer.clear();
        parse_next_arg:
            switch (ptr->type)
            {
                case token_type::kw_null: arg_buffer.push_back(constant(nullptr)); ptr++; goto parse_next_arg;
                case token_type::literal: arg_buffer.push_back(parse_constant(ptr)); goto parse_next_arg;
                default:
                {
                    if (ptr->type <= token_type::op_dump) break;
                    arg_buffer.push_back(parse_address(ptr));
                    goto parse_next_arg;
                }
            }

            current_method->write_call(declare_method(method), arg_buffer);
        }
        void write_callv(const token*& ptr)
        {
            const address addr = parse_address(ptr);

            arg_buffer.clear();
        parse_next_arg:
            switch (ptr->type)
            {
                case token_type::kw_null: arg_buffer.push_back(constant(nullptr)); ptr++; goto parse_next_arg;
                case token_type::literal: arg_buffer.push_back(parse_constant(ptr)); goto parse_next_arg;
                default:
                {
                    if (ptr->type <= token_type::op_dump) break;
                    arg_buffer.push_back(parse_address(ptr));
                    goto parse_next_arg;
                }
            }

            current_method->write_callv(addr, arg_buffer);
        }
        void write_ret()
        {
            current_method->write_ret();
        }
        void write_retv(const token*& ptr)
        {
            const address addr = parse_address(ptr);
            current_method->write_retv(addr);
        }

        void write_dump(const token*& ptr)
        {
            const address addr = parse_address(ptr);
            current_method->write_dump(addr);
        }


        void write_label(string_view label_name)
        {
            current_method->write_label(current_method->declare_label(label_name));
        }

        type_idx parse_typename(const token*& ptr, size_t depth = 0)
        {
            UNEXPECTED_EXPRESSION(ptr->type == token_type::identifier, ptr->str);
            set_line_number(ptr->line_num);
            const string_view base_name = (ptr++)->str;

            type_idx index = declare_type(base_name);

        parse_next_modifier:
            switch (ptr->type)
            {
                case token_type::asterisk:
                {
                    index = declare_pointer_type(index);
                    ptr++;
                    goto parse_next_modifier;
                }

                case token_type::lbracket:
                {
                    ptr++;
                    UNEXPECTED_EXPRESSION(ptr->type == token_type::literal, ptr->str);
                    auto size = parse_ulong(ptr->str);
                    LITERAL_PARSE_FAILURE(size.is_valid(), ptr->str);
                    VALIDATE_ARRAY_SIZE(size.value);
                    ptr++;
                    UNEXPECTED_END(ptr->type == token_type::rbracket);
                    ptr++;
                    index = declare_array_type(index, size_t(size.value));
                    goto parse_next_modifier;
                }

                case token_type::lparen:
                {
                    ptr++;
                    vector<type_idx> param_buffer;
                parse_next_parameter:
                    param_buffer.push_back(parse_typename(ptr, depth + 1));
                    switch (ptr->type)
                    {
                        case token_type::comma: ptr++; goto parse_next_parameter;
                        case token_type::rparen: ptr++; break;
                        default: UNEXPECTED_EXPRESSION(false, ptr->str);
                    }
                    index = declare_signature_type(make_signature(index, param_buffer));
                    goto parse_next_modifier;
                }
            }

            return index;
        }
        type_idx parse_parameter(const token*& ptr, bool is_parameter)
        {
            switch (ptr->type)
            {
                case token_type::identifier:
                {
                    // <type> <identifier>
                    const type_idx type = parse_typename(ptr);
                    if (is_parameter)
                    {
                        const auto find = parameter_lookup.names.find(ptr->str);
                        DUPLICATE_STACK_NAME(!find, ptr->str);
                        parameter_lookup.names.emplace(ptr->str, parameter_lookup.count++);
                    }
                    else
                    {
                        const auto find = stackvar_lookup.names.find(ptr->str);
                        DUPLICATE_STACK_NAME(!find, ptr->str);
                        stackvar_lookup.names.emplace(ptr->str, stackvar_lookup.count++);
                    }
                    ptr++;
                    return type;
                }

                case token_type::literal:
                {
                    // <number>: <type>
                    const auto parse = parse_ulong(ptr->str);
                    LITERAL_PARSE_FAILURE(parse.is_valid(), ptr->str);
                    VALIDATE_STACK_INDEX(parse.value);
                    const index_t index = (index_t)parse.value;
                    ptr++;
                    UNEXPECTED_EXPRESSION(ptr->type == token_type::colon, ptr->str);
                    ptr++;
                    const type_idx type = parse_typename(ptr);
                    if (is_parameter)
                    {
                        DUPLICATE_PARAM_IDX(parameter_lookup.indices.find(index) == parameter_lookup.indices.end(), index);
                        parameter_lookup.indices.emplace(index, parameter_lookup.count++);
                    }
                    else
                    {
                        DUPLICATE_STACK_IDX(stackvar_lookup.indices.find(index) == stackvar_lookup.indices.end(), index);
                        stackvar_lookup.indices.emplace(index, stackvar_lookup.count++);
                    }
                    return type;
                }

                default: UNEXPECTED_EXPRESSION(false, ptr->str);
            }
            return type_idx::invalid;
        }

        template<typename value_t> value_t parse_offset_num(const token*& ptr)
        {
            UNEXPECTED_EXPRESSION(ptr->type == token_type::literal, ptr->str);
            auto num = parse_int_literal_cast<value_t>(ptr->str);
            LITERAL_PARSE_FAILURE(num.is_valid(), ptr->str);
            ptr++;
            return num.value;
        }

        vector<name_idx> field_names;
        address parse_address(const token*& ptr)
        {
            set_line_number(ptr->line_num);
            address result(0, address_type::stackvar);

            // Prefix
            switch (ptr->type)
            {
                case token_type::kw_null: ptr++; return constant(nullptr);
                case token_type::literal: return parse_constant(ptr);

                case token_type::asterisk: ptr++; result.header.set_prefix(address_prefix::indirection); break;
                case token_type::ampersand: ptr++; result.header.set_prefix(address_prefix::address_of); break;
                case token_type::exclamation: ptr++; result.header.set_prefix(address_prefix::size_of); break;

                default: break;
            }

            // Type
            const token_type type = ptr->type;
            switch (ptr->type)
            {
                case token_type::literal: UNEXPECTED_LITERAL(false); break;

                case token_type::identifier:
                {
                    const string_view name = (ptr++)->str;

                    if (const auto find = stackvar_lookup.names.find(name))
                    {
                        // Named stackvar
                        result.header.set_type(address_type::stackvar);
                        result.header.set_index(*find);
                        break;
                    }

                    if (const auto find = parameter_lookup.names.find(name))
                    {
                        // Named parameter
                        result.header.set_type(address_type::parameter);
                        result.header.set_index(*find);
                        break;
                    }

                    // Global
                    result.header.set_type(address_type::global);
                    result.header.set_index((index_t)make_identifier(name));
                }
                break;

                case token_type::lbrace:
                {
                    ptr++;
                    if (ptr->type == token_type::circumflex)
                    {
                        // Return address
                        ptr++;
                        result.header.set_type(address_type::stackvar);
                        result.header.set_index(address_header_constants::index_max);
                    }
                    else
                    {
                        const index_t parse_idx = parse_offset_num<index_t>(ptr);
                        const auto find = stackvar_lookup.indices.find(parse_idx);
                        UNDEFINED_STACK_IDX(find != stackvar_lookup.indices.end(), parse_idx);
                        result.header.set_type(address_type::stackvar);
                        result.header.set_index(find->second);
                    }
                    UNEXPECTED_EXPRESSION(ptr->type == token_type::rbrace, ptr->str);
                    ptr++;
                }
                break;

                case token_type::lparen:
                {
                    ptr++;
                    const index_t parse_idx = parse_offset_num<index_t>(ptr);
                    const auto find = parameter_lookup.indices.find(parse_idx);
                    UNDEFINED_STACK_IDX(find != parameter_lookup.indices.end(), parse_idx);
                    UNEXPECTED_EXPRESSION(ptr->type == token_type::rparen, ptr->str);
                    ptr++;
                    result.header.set_type(address_type::parameter);
                    result.header.set_index(find->second);
                }
                break;

                default: UNEXPECTED_EXPRESSION(false, ptr->str);
            }

            // Modifier
            switch (ptr->type)
            {
                case token_type::period:
                case token_type::deref:
                {
                    const address_modifier modifier_type = ptr->type == token_type::deref ? address_modifier::indirect_field : address_modifier::direct_field;
                    ptr++;
                    UNEXPECTED_EXPRESSION(ptr->type == token_type::identifier, ptr->str);
                    const type_idx object_type = declare_type((ptr++)->str);
                    UNEXPECTED_EXPRESSION(ptr->type == token_type::colon, ptr->str);
                    ptr++;

                    field_names.clear();
                parse_next_field:
                    UNEXPECTED_EXPRESSION(ptr->type == token_type::identifier, ptr->str);
                    field_names.push_back(make_identifier((ptr++)->str));
                    if (ptr->type == token_type::period)
                    {
                        ptr++;
                        goto parse_next_field;
                    }

                    result.header.set_modifier(modifier_type);
                    result.payload.field = make_offset(object_type, field_names);
                }
                break;

                case token_type::lbracket:
                {
                    ptr++;
                    const offset_t offset = parse_offset_num<offset_t>(ptr);
                    UNEXPECTED_EXPRESSION(ptr->type == token_type::rbracket, ptr->str);
                    ptr++;
                    result.header.set_modifier(address_modifier::offset);
                    result.payload.offset = offset;
                }
                break;

                default: break;
            }

            return result;
        }
        constant parse_constant(const token*& ptr)
        {
            if (ptr->type == token_type::kw_null)
            {
                ptr++;
                return constant(nullptr);
            }

            UNEXPECTED_EXPRESSION(ptr->type == token_type::literal, ptr->str);

            const auto result = parse_literal(ptr->str);
            LITERAL_PARSE_FAILURE(result.is_valid(), ptr->str);
            ptr++;

            switch (result.type)
            {
                case type_idx::i8:  return constant(result.value.as_i8);
                case type_idx::u8:  return constant(result.value.as_u8);
                case type_idx::i16: return constant(result.value.as_i16);
                case type_idx::u16: return constant(result.value.as_u16);
                case type_idx::i32: return constant(result.value.as_i32);
                case type_idx::u32: return constant(result.value.as_u32);
                case type_idx::i64: return constant(result.value.as_i64);
                case type_idx::u64: return constant(result.value.as_u64);
                case type_idx::f32: return constant(result.value.as_f32);
                case type_idx::f64: return constant(result.value.as_f64);
                default: ASSERT(false, "Invalid constant type"); return constant(0);
            }
        }

        // Parser state
        definition_type current_scope = definition_type::none;
        vector<token> tokens;
        index_t line_num;

        // Locals
        class variable_lookup
        {
        public:
            unordered_map<index_t, index_t> indices;
            database<name_idx, index_t> names;
            index_t count = 0;

            inline void clear() noexcept
            {
                indices.clear();
                names.clear();
                count = 0;
            }
        };
        variable_lookup stackvar_lookup;
        variable_lookup parameter_lookup;

        type_writer* current_type = nullptr;
        method_writer* current_method = nullptr;

        // Reusable buffers
        vector<type_idx> parameters;
    };

    intermediate parser_propane::parse(const char* file_path)
    {
        return parser_impl(file_path).finalize();
    }
}