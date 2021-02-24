#include "propane_generator.hpp"
#include "database.hpp"
#include "utility.hpp"
#include "errors.hpp"
#include "literals.hpp"
#include "internal.hpp"

#include <charconv>
#include <system_error>

#define VALIDATE(errc, expr, fmt, ...) ENSURE_WITH_META(errc, this->get_meta(), expr, propane::generator_exception, fmt, __VA_ARGS__)

#define VALIDATE_FILE_OPEN(file_open, file_path) VALIDATE(ERRC::PRS_FILE_EXCEPTION, (file_open), \
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
#define VALIDATE_STACK_INDEX(num) VALIDATE(ERRC::PRS_STACK_IDX_OVERFLOW, uint64_t(num) < uint64_t(address_header::index_max), \
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
	enum class definition_type
	{
		none = 0,
		object,
		method,
		param,
		stack,
		global,
		constant,
	};

	enum class comment_type
	{
		none = 0,
		single,
		multi,
	};

	// Experimental implementation of propane generator
	class intermediate_parser final : public propane_generator
	{
	public:
		NOCOPY_CLASS_DEFAULT(intermediate_parser) = delete;

		intermediate_parser(const char* file_path) :
			propane_generator(strip_filepath(file_path))
		{
			string file_text;

			// Read file (and close thereafter)
			{
				ifstream file(file_path);
				VALIDATE_FILE_OPEN(file.is_open(), file_path);
				file_text = (stringstream() << file.rdbuf() << '\n').str();
			}

			const char* beg = file_text.data();
			const char* end = beg + file_text.size();
			index_t line_number = 1;
			set_line_number(line_number);
			vector<string_view> tokens;
			comment_type comment = comment_type::none;
			for (const char* it = beg; it < end; it++)
			{
				if (*it == '\n' || *it == '\t' || *it == ' ' || *it == '\r')
				{
					if (comment == comment_type::none)
					{
						const auto len = it - beg;
						if (len > 0)
						{
							tokens.push_back(string_view(beg, len));
						}
					}

					if (*it == '\n')
					{
						if (!tokens.empty())
						{
							evaluate(tokens.data(), tokens.size());
						}

						tokens.clear();
						if (comment == comment_type::single)
						{
							comment = comment_type::none;
						}

						set_line_number(++line_number);
					}

					beg = it + 1;
				}
				else if (comment == comment_type::none)
				{
					if (*it == '/')
					{
						const char next = *(it + 1);
						if (next == '/' || next == '*')
						{
							comment = next == '*' ? comment_type::multi : comment_type::single;
							const auto len = it - beg;
							if (len > 0)
							{
								tokens.push_back(string_view(beg, len));
							}
							it++;
						}
					}
				}
				else if (comment == comment_type::multi)
				{
					if (*it == '*' && *(it + 1) == '/')
					{
						comment = comment_type::none;
						it++;
						beg = it + 1;
					}
				}
			}

			UNEXPECTED_EOF(current_scope == definition_type::none);
			UNTERMINATED_COMMENT(comment != comment_type::multi);
		}

	private:
		void evaluate(string_view* tokens, size_t num)
		{
			if (current_scope != definition_type::none)
			{
				if (num == 1)
				{
					if (tokens[0] == "end") return end();
				}
			}

			switch (current_scope)
			{
				case definition_type::none:
				{
					if (num == 1)
					{
						if (tokens[0] == "global") return begin_global();
						if (tokens[0] == "constant") return begin_constant();
					}
					if (num >= 2)
					{
						if (tokens[0] == "method") return begin_method(tokens[1], tokens + 2, num - 2);
					}
					if (num == 2)
					{
						if (tokens[0] == "struct") return begin_struct(tokens[1], false);
						if (tokens[0] == "union") return begin_struct(tokens[1], true);
					}
				}
				break;

				case definition_type::object:
				{
					if (num == 2)
					{
						return field(tokens[0], tokens[1]);
					}
				}
				break;

				case definition_type::method:
				{
					if (num == 1)
					{
						if (tokens[0] == "noop") return write_noop();

						if (tokens[0] == "ret") return write_ret();

						if (tokens[0].size() > 1 && tokens[0].back() == ':') return write_label(tokens[0].substr(0, tokens[0].size() - 1));
					}
					if (num >= 1)
					{
						if (tokens[0] == "stack") return begin_stack(tokens + 1, num - 1);
					}
					if (num >= 2)
					{
						if (tokens[0] == "call") return write_call(tokens[1], tokens + 2, num - 2);
						if (tokens[0] == "callv") return write_callv(tokens[1], tokens + 2, num - 2);
					}
					if (num >= 3)
					{
						if (tokens[0] == "sw") return write_sw(tokens[1], tokens + 2, num - 2);
					}
					if (num == 2)
					{
						if (tokens[0] == "retv") return write_retv(tokens[1]);

						if (tokens[0] == "not") return write_not(tokens[1]);
						if (tokens[0] == "neg") return write_neg(tokens[1]);

						if (tokens[0] == "cze") return write_cze(tokens[1]);
						if (tokens[0] == "cnz") return write_cnz(tokens[1]);

						if (tokens[0] == "br") return write_br(tokens[1]);

						if (tokens[0] == "dump") return write_dump(tokens[1]);
					}
					if (num == 3)
					{
						if (tokens[0] == "set") return write_set(tokens[1], tokens[2]);
						if (tokens[0] == "conv") return write_conv(tokens[1], tokens[2]);

						if (tokens[0] == "mul") return write_mul(tokens[1], tokens[2]);
						if (tokens[0] == "div") return write_div(tokens[1], tokens[2]);
						if (tokens[0] == "mod") return write_mod(tokens[1], tokens[2]);
						if (tokens[0] == "add") return write_add(tokens[1], tokens[2]);
						if (tokens[0] == "sub") return write_sub(tokens[1], tokens[2]);
						if (tokens[0] == "lsh") return write_lsh(tokens[1], tokens[2]);
						if (tokens[0] == "rsh") return write_rsh(tokens[1], tokens[2]);
						if (tokens[0] == "and") return write_and(tokens[1], tokens[2]);
						if (tokens[0] == "xor") return write_xor(tokens[1], tokens[2]);
						if (tokens[0] == "or") return write_or(tokens[1], tokens[2]);

						if (tokens[0] == "padd") return write_padd(tokens[1], tokens[2]);
						if (tokens[0] == "psub") return write_psub(tokens[1], tokens[2]);
						if (tokens[0] == "pdif") return write_pdif(tokens[1], tokens[2]);

						if (tokens[0] == "cmp") return write_cmp(tokens[1], tokens[2]);
						if (tokens[0] == "ceq") return write_ceq(tokens[1], tokens[2]);
						if (tokens[0] == "cne") return write_cne(tokens[1], tokens[2]);
						if (tokens[0] == "cgt") return write_cgt(tokens[1], tokens[2]);
						if (tokens[0] == "cge") return write_cge(tokens[1], tokens[2]);
						if (tokens[0] == "clt") return write_clt(tokens[1], tokens[2]);
						if (tokens[0] == "cle") return write_cle(tokens[1], tokens[2]);

						if (tokens[0] == "bze") return write_bze(tokens[1], tokens[2]);
						if (tokens[0] == "bnz") return write_bnz(tokens[1], tokens[2]);
					}
					if (num == 4)
					{
						if (tokens[0] == "beq") return write_beq(tokens[1], tokens[2], tokens[3]);
						if (tokens[0] == "bne") return write_bne(tokens[1], tokens[2], tokens[3]);
						if (tokens[0] == "bgt") return write_bgt(tokens[1], tokens[2], tokens[3]);
						if (tokens[0] == "bge") return write_bge(tokens[1], tokens[2], tokens[3]);
						if (tokens[0] == "blt") return write_blt(tokens[1], tokens[2], tokens[3]);
						if (tokens[0] == "ble") return write_ble(tokens[1], tokens[2], tokens[3]);
					}
				}
				break;

				case definition_type::param:
				{
					return param(tokens, num);
				}
				break;

				case definition_type::stack:
				{
					return stack(tokens, num);
				}
				break;

				case definition_type::global:
				{
					if (num >= 2) return global(tokens[0], false, tokens[1], tokens + 2, num - 2);
				}
				break;

				case definition_type::constant:
				{
					if (num >= 2) return global(tokens[0], true, tokens[1], tokens + 2, num - 2);
				}
				break;
			}

			UNEXPECTED_EXPRESSION(false, tokens[0]);
		}


		void begin_struct(string_view obj_name, bool is_union)
		{
			current_scope = definition_type::object;

			current_type = &define_type(obj_name, is_union);
		}
		void field(string_view type_name, string_view field_name)
		{
			current_type->declare_field(resolve_typename(type_name), field_name);
		}

		void begin_method(string_view method_name, string_view* args, size_t num)
		{
			current_scope = definition_type::method;

			parameters.clear();

			method_return_type = type_idx::voidtype;
			if (num > 0 && args[0] == "returns")
			{
				method_return_type = resolve_typename(args[1]);

				args += 2;
				num -= 2;
			}

			method_index = declare_method(method_name);
			if (num > 0 && args[0] == "parameters")
			{
				current_scope = definition_type::param;
				return param(args + 1, num - 1);
			}

			UNEXPECTED_EXPRESSION(num == 0, args[0]);

			current_method = &define_method(method_index, make_signature(method_return_type, parameters));
		}

		void begin_stack(string_view* args, size_t num)
		{
			current_scope = definition_type::stack;

			if (num > 0)
			{
				return stack(args, num);
			}
		}
		void param(string_view* args, size_t num)
		{
			while (num > 0)
			{
				if (num == 1 && args[0] == "end")
				{
					end();
					break;
				}

				parameters.push_back(parse_parameters(args, num, parameter_lookup));
			}
		}
		void stack(string_view* args, size_t num)
		{
			while (num > 0)
			{
				if (num == 1 && args[0] == "end")
				{
					end();
					break;
				}

				stackvars.push_back(parse_parameters(args, num, stackvar_lookup));
			}
		}

		void begin_global()
		{
			current_scope = definition_type::global;
		}
		void begin_constant()
		{
			current_scope = definition_type::constant;
		}
		void global(string_view type_name, bool is_constant, string_view global_name, string_view* args, size_t num)
		{
			vector<constant> init;
			for (size_t i = 0; i < num; i++)
			{
				if (is_identifier(args[i]))
				{
					init.push_back(make_identifier(args[i]));
				}
				else
				{
					init.push_back(read_constant(args[i]));
				}
			}

			define_global(make_identifier(global_name), is_constant, resolve_typename(type_name), init);
		}


		void end()
		{
			switch (current_scope)
			{
				case definition_type::object:
				{
					current_type->finalize();

					current_scope = definition_type::none;
				}
				break;

				case definition_type::method:
				{
					stackvar_lookup.clear();
					parameter_lookup.clear();

					current_method->finalize();

					current_scope = definition_type::none;
				}
				break;

				case definition_type::param:
				{
					current_method = &define_method(method_index, make_signature(method_return_type, parameters));

					parameters.clear();

					current_scope = definition_type::method;
				}
				break;

				case definition_type::stack:
				{
					current_method->set_stack(stackvars);

					stackvars.clear();

					current_scope = definition_type::method;
				}
				break;

				case definition_type::global:
				case definition_type::constant:
				{
					current_scope = definition_type::none;
				}
				break;

				default: UNEXPECTED_END(false);
			}
		}


		void write_noop()
		{
			current_method->write_noop();
		}

		void write_set(string_view lhs, string_view rhs)
		{
			current_method->write_set(read_address(lhs), read_address(rhs));
		}
		void write_conv(string_view lhs, string_view rhs)
		{
			current_method->write_conv(read_address(lhs), read_address(rhs));
		}

		void write_not(string_view addr)
		{
			current_method->write_not(read_address(addr));
		}
		void write_neg(string_view addr)
		{
			current_method->write_neg(read_address(addr));
		}
		void write_mul(string_view lhs, string_view rhs)
		{
			current_method->write_mul(read_address(lhs), read_address(rhs));
		}
		void write_div(string_view lhs, string_view rhs)
		{
			current_method->write_div(read_address(lhs), read_address(rhs));
		}
		void write_mod(string_view lhs, string_view rhs)
		{
			current_method->write_mod(read_address(lhs), read_address(rhs));
		}
		void write_add(string_view lhs, string_view rhs)
		{
			current_method->write_add(read_address(lhs), read_address(rhs));
		}
		void write_sub(string_view lhs, string_view rhs)
		{
			current_method->write_sub(read_address(lhs), read_address(rhs));
		}
		void write_lsh(string_view lhs, string_view rhs)
		{
			current_method->write_lsh(read_address(lhs), read_address(rhs));
		}
		void write_rsh(string_view lhs, string_view rhs)
		{
			current_method->write_rsh(read_address(lhs), read_address(rhs));
		}
		void write_and(string_view lhs, string_view rhs)
		{
			current_method->write_and(read_address(lhs), read_address(rhs));
		}
		void write_xor(string_view lhs, string_view rhs)
		{
			current_method->write_xor(read_address(lhs), read_address(rhs));
		}
		void write_or(string_view lhs, string_view rhs)
		{
			current_method->write_or(read_address(lhs), read_address(rhs));
		}

		void write_padd(string_view lhs, string_view rhs)
		{
			current_method->write_padd(read_address(lhs), read_address(rhs));
		}
		void write_psub(string_view lhs, string_view rhs)
		{
			current_method->write_psub(read_address(lhs), read_address(rhs));
		}
		void write_pdif(string_view lhs, string_view rhs)
		{
			current_method->write_pdif(read_address(lhs), read_address(rhs));
		}

		void write_cmp(string_view lhs, string_view rhs)
		{
			current_method->write_cmp(read_address(lhs), read_address(rhs));
		}
		void write_ceq(string_view lhs, string_view rhs)
		{
			current_method->write_ceq(read_address(lhs), read_address(rhs));
		}
		void write_cne(string_view lhs, string_view rhs)
		{
			current_method->write_cne(read_address(lhs), read_address(rhs));
		}
		void write_cgt(string_view lhs, string_view rhs)
		{
			current_method->write_cgt(read_address(lhs), read_address(rhs));
		}
		void write_cge(string_view lhs, string_view rhs)
		{
			current_method->write_cge(read_address(lhs), read_address(rhs));
		}
		void write_clt(string_view lhs, string_view rhs)
		{
			current_method->write_clt(read_address(lhs), read_address(rhs));
		}
		void write_cle(string_view lhs, string_view rhs)
		{
			current_method->write_cle(read_address(lhs), read_address(rhs));
		}
		void write_cze(string_view addr)
		{
			current_method->write_cze(read_address(addr));
		}
		void write_cnz(string_view addr)
		{
			current_method->write_cnz(read_address(addr));
		}

		void write_br(string_view label)
		{
			current_method->write_br(current_method->declare_label(label));
		}
		void write_beq(string_view label, string_view lhs, string_view rhs)
		{
			current_method->write_beq(current_method->declare_label(label), read_address(lhs), read_address(rhs));
		}
		void write_bne(string_view label, string_view lhs, string_view rhs)
		{
			current_method->write_bne(current_method->declare_label(label), read_address(lhs), read_address(rhs));
		}
		void write_bgt(string_view label, string_view lhs, string_view rhs)
		{
			current_method->write_bgt(current_method->declare_label(label), read_address(lhs), read_address(rhs));
		}
		void write_bge(string_view label, string_view lhs, string_view rhs)
		{
			current_method->write_bge(current_method->declare_label(label), read_address(lhs), read_address(rhs));
		}
		void write_blt(string_view label, string_view lhs, string_view rhs)
		{
			current_method->write_blt(current_method->declare_label(label), read_address(lhs), read_address(rhs));
		}
		void write_ble(string_view label, string_view lhs, string_view rhs)
		{
			current_method->write_ble(current_method->declare_label(label), read_address(lhs), read_address(rhs));
		}
		void write_bze(string_view label, string_view addr)
		{
			current_method->write_bze(current_method->declare_label(label), read_address(addr));
		}
		void write_bnz(string_view label, string_view addr)
		{
			current_method->write_bnz(current_method->declare_label(label), read_address(addr));
		}

		void write_sw(string_view addr, string_view* args, size_t num)
		{
			vector<label_idx> labels;
			for (size_t i = 0; i < num; i++)
			{
				labels.push_back(current_method->declare_label(args[i]));
			}

			current_method->write_sw(read_address(addr), labels);
		}

		void write_call(string_view method, string_view* args, size_t num)
		{
			vector<address> arg;
			for (size_t i = 0; i < num; i++)
			{
				arg.push_back(read_address(args[i]));
			}

			current_method->write_call(declare_method(method), arg);
		}
		void write_callv(string_view addr, string_view* args, size_t num)
		{
			vector<address> arg;
			for (size_t i = 0; i < num; i++)
			{
				arg.push_back(read_address(args[i]));
			}

			current_method->write_callv(read_address(addr), arg);
		}
		void write_ret()
		{
			current_method->write_ret();
		}
		void write_retv(string_view addr)
		{
			current_method->write_retv(read_address(addr));
		}

		void write_dump(string_view addr)
		{
			current_method->write_dump(read_address(addr));
		}


		void write_label(string_view label_name)
		{
			current_method->write_label(current_method->declare_label(label_name));
		}


		type_idx resolve_typename(string_view type_name)
		{
			type_idx index = type_idx::invalid;

			const char* beg = type_name.data();
			const char* end = beg + type_name.size();

			// Find base name (without array/pointer decorators)
			const char* base = beg;
			for (; base < end; ++base)
			{
				if (*base == '(' || *base == '[' || *base == '*')
				{
					break;
				}
			}

			UNEXPECTED_CHARACTER(base > beg, *beg);
			string_view base_name(beg, base - beg);

			index = declare_type(base_name);

			// Resolve decorators
			while (base < end)
			{
				if (*base == '*')
				{
					index = declare_pointer_type(index);
					base++;
					continue;
				}

				if (*base == '[')
				{
					base++;
					auto offset = end - base;
					if (offset > 0)
					{
						auto size = parse_ulong(base, end);
						if (base < end && base[0] == ']')
						{
							VALIDATE_ARRAY_SIZE(size.value);
							index = declare_array_type(index, size_t(size.value));
							base++;
							continue;
						}
					}
				}

				// Resolve signature
				if (*base == '(')
				{
					type_idx return_type = index;
					vector<type_idx> parameters;
					base++;
					size_t level = 1;
					for (const char* c = base; c < end; c++)
					{
						if (*c == '(') level++;
						if (level == 1 && (*c == ',' || *c == ')'))
						{
							const auto offset = c - base;
							UNEXPECTED_CHARACTER(offset > 0 || *c == ')', *c);

							if (offset > 0)
							{
								string_view param(base, offset);
								parameters.push_back(resolve_typename(param));
							}

							base = c + 1;

							if (*c == ')')
							{
								level = 0;
								break;
							}
						}
						if (*c == ')') level--;
					}

					UNTERMINATED_CHARACTER(level == 0, '(');
					const signature_idx sig_idx = make_signature(return_type, parameters);
					index = declare_signature_type(sig_idx);
					continue;
				}

				UNEXPECTED_CHARACTER(beg != base, *base);
			}

			return index;
		}


		// Parser state
		definition_type current_scope = definition_type::none;

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

		method_idx method_index = method_idx::invalid;
		type_idx method_return_type = type_idx::invalid;
		vector<type_idx> stackvars;
		vector<type_idx> parameters;



		type_idx parse_parameters(string_view*& args, size_t& num, variable_lookup& lookup)
		{
			if (num >= 2)
			{
				type_idx type;
				if (args[0].back() == ':')
				{
					// <number>: <type>
					auto parse = parse_ulong(args[0].substr(0, args[0].length() - 1));
					LITERAL_PARSE_FAILURE(parse.is_valid(), args[0]);
					VALIDATE_STACK_INDEX(parse.value);
					index_t index = (index_t)parse.value;

					type = resolve_typename(args[1]);

					if (current_scope == definition_type::param)
					{
						DUPLICATE_PARAM_IDX(lookup.indices.find(index) == lookup.indices.end(), index);
					}
					else
					{
						DUPLICATE_STACK_IDX(lookup.indices.find(index) == lookup.indices.end(), index);
					}
					lookup.indices.emplace(index, lookup.count++);
				}
				else
				{
					// <type> <identifier>
					type = resolve_typename(args[0]);

					auto find = lookup.names.find(args[1]);
					DUPLICATE_STACK_NAME(!find, args[1]);

					lookup.names.emplace(args[1], lookup.count++);
				}

				args += 2;
				num -= 2;

				return type;
			}

			UNEXPECTED_EXPRESSION(num == 0, args[0]);
			return type_idx::invalid;
		}

		template<typename value_t> bool parse_offset_num(const char*& c, const char* end, char open, char close, value_t& out_val)
		{
			if (*c == open)
			{
				c += 1;
				// Find closing delimiter
				bool found = false;
				for (const char* ptr = c; ptr < end; ptr++)
				{
					if (*ptr == close)
					{
						found = true;
						end = ptr;
						break;
					}
				}
				if (found)
				{
					// Parse number
					auto num = parse_integer<value_t>(c, end);
					LITERAL_PARSE_FAILURE(num.is_valid(), string_view(c, end - c));
					UNEXPECTED_CHARACTER(*c == close, *c);
					c += 1;
					out_val = num.value;
					return true;
				}
			}
			return false;
		}
		template<typename value_t> bool parse_offset_num(string_view str, char open, char close, value_t& out_val)
		{
			const char* c = str.data();
			const char* end = c + str.size();
			bool result = parse_offset_num(c, end, open, close, out_val);
			return result && c == end;
		}
		template<size_t len> bool parse_addr_str(const char*& c, const char* end, const char(&in_arr)[len])
		{
			constexpr size_t str_len = len - 1;
			const size_t offset = (end - c);
			if (offset >= str_len)
			{
				if (memcmp(c, in_arr, str_len) == 0)
				{
					c += str_len;
					return true;
				}
			}
			return false;
		}

		address read_address(string_view str)
		{
			if (is_literal(str))
			{
				return read_constant(str);
			}

			const char* c = str.data();
			const char* const end = c + str.size();

			address result(0, address_type::stackvar);

			// Prefix
			switch (*c)
			{
				case '*': result.header.set_prefix(address_prefix::indirection); c++; break;
				case '&': result.header.set_prefix(address_prefix::address_of); c++; break;
				case '!': result.header.set_prefix(address_prefix::size_of); c++; break;
			}
			UNEXPECTED_CHARACTER(c < end, *c);

			UNEXPECTED_LITERAL(!is_literal(string_view(c, end - c)));

			// Type
			while (c < end)
			{
				if (parse_addr_str(c, end, "{^}"))
				{
					// Return address
					result.header.set_type(address_type::stackvar);
					result.header.set_index(address_header::index_max);
					break;
				}

				index_t parse_idx;
				if (parse_offset_num(c, end, '{', '}', parse_idx))
				{
					// Indexed stackvar
					auto find = stackvar_lookup.indices.find(parse_idx);
					UNDEFINED_STACK_IDX(find != stackvar_lookup.indices.end(), parse_idx);
					result.header.set_type(address_type::stackvar);
					result.header.set_index(find->second);
					break;
				}

				if (parse_offset_num(c, end, '(', ')', parse_idx))
				{
					// Indexed parameter
					auto find = parameter_lookup.indices.find(parse_idx);
					UNDEFINED_PARAM_IDX(find != parameter_lookup.indices.end(), parse_idx);
					result.header.set_type(address_type::parameter);
					result.header.set_index(find->second);
					break;
				}

				if (is_identifier(*c))
				{
					const char* beg = c;
					string_view name;
					for (c = beg; c <= end; c++)
					{
						if (!is_identifier(*c) || c == end)
						{
							UNEXPECTED_CHARACTER(c > beg, *c);
							name = string_view(beg, c - beg);
							break;
						}
					}

					if (auto find = stackvar_lookup.names.find(name))
					{
						// Named stackvar
						result.header.set_type(address_type::stackvar);
						result.header.set_index(*find);
						break;
					}

					if (auto find = parameter_lookup.names.find(name))
					{
						// Named parameter
						result.header.set_type(address_type::parameter);
						result.header.set_index(*find);
						break;
					}

					// Global
					result.header.set_type(address_type::global);
					result.header.set_index((index_t)make_identifier(name));
					break;
				}

				UNEXPECTED_CHARACTER(false, *c);
			}

			// Modifier
			while (c < end)
			{
				offset_t offset;
				if (parse_offset_num(c, end, '[', ']', offset))
				{
					result.header.set_modifier(address_modifier::subscript);
					result.payload.offset = offset;
					break;
				}

				if (*c == '.' || *c == '-')
				{
					// Check for ->
					const bool is_deref = *c == '-';
					if (is_deref)
					{
						c++;
						UNEXPECTED_CHARACTER(*c == '>', *c);
					}

					// Parse field offset address
					const char* beg = c + 1;
					type_idx parent_type = type_idx::invalid;
					vector<name_idx> field_names;
					for (c = beg; c <= end; c++)
					{
						if (c < end && *c == ':')
						{
							UNEXPECTED_CHARACTER(parent_type == type_idx::invalid, *c);
							parent_type = declare_type(string_view(beg, c - beg));
							beg = c + 1;
						}
						else if (*c == '.')
						{
							UNEXPECTED_CHARACTER(c > beg && parent_type != type_idx::invalid, *c);
							field_names.push_back(make_identifier(string_view(beg, c - beg)));
							beg = c + 1;
						}
						else if (c == end)
						{
							UNEXPECTED_CHARACTER(c > beg && parent_type != type_idx::invalid, *c);
							field_names.push_back(make_identifier(string_view(beg, c - beg)));
							break;
						}
					}

					result.header.set_modifier(is_deref ? address_modifier::indirect_field : address_modifier::direct_field);
					result.payload.field = make_offset(parent_type, field_names);

					break;
				}

				UNEXPECTED_CHARACTER(false, *c);
			}

			UNEXPECTED_CHARACTER(c == end, *c);

			return result;
		}
		constant read_constant(string_view str)
		{
			if (str == null_keyword)
			{
				return constant(nullptr);
			}

			const char* beg = str.data();
			const char* end = str.data() + str.size();

			const bool negate = parse_negate(beg, end);
			const int32_t base = parse_integer_base(beg, end);

			// Check for float (.)
			bool is_float = false;
			for (const char* c = beg; c < end; c++)
			{
				UNEXPECTED_CHARACTER(*c != '-', '-');
				if (*c == '.')
				{
					UNEXPECTED_CHARACTER(!is_float && base == 10, '.');
					is_float = true;
				}
			}

			// Check for float (f-suffix)
			const char last = *(end - 1);
			if (base == 10 && (last == 'f' || last == 'F'))
			{
				is_float = true;
			}

			if (is_float)
			{
				type_idx btype = type_idx::f64;
				double f;
				std::from_chars_result result = std::from_chars(beg, end, f);
				if (result.ptr < end)
				{
					beg = result.ptr;

					const auto offset = end - beg;
					if (offset == 1 && (beg[0] == 'f' || beg[0] == 'F'))
					{
						btype = type_idx::f32;
					}
					else
					{
						LITERAL_PARSE_FAILURE(false, str);
					}
				}

				switch (btype)
				{
					case type_idx::f32: { return constant(negate ? -f32(f) : f32(f)); break; }
					case type_idx::f64: { return constant(negate ? -f64(f) : f64(f)); break; }
					default: ASSERT(false, "Unhandled case");
				}
			}
			else
			{
				// First, parse the biggest number we can support
				auto as_ulong = parse_ulong(beg, end, base);
				LITERAL_PARSE_FAILURE(as_ulong.is_valid(), string_view(beg, end - beg));
				
				// Then, find the smallest number that fits
				type_idx btype = determine_integer_type(as_ulong.value, beg, end);
				UNEXPECTED_CHARACTER(beg == end, *beg);

				// Finally, cast to that type
				uint64_t i = as_ulong.value;
				switch (btype)
				{
					case type_idx::i8: { return constant(negate_num(i8(i), negate)); break; }
					case type_idx::u8: { return constant(negate_num(u8(i), negate)); break; }
					case type_idx::i16: { return constant(negate_num(i16(i), negate)); break; }
					case type_idx::u16: { return constant(negate_num(u16(i), negate)); break; }
					case type_idx::i32: { return constant(negate_num(i32(i), negate)); break; }
					case type_idx::u32: { return constant(negate_num(u32(i), negate)); break; }
					case type_idx::i64: { return constant(negate_num(i64(i), negate)); break; }
					case type_idx::u64: { return constant(negate_num(u64(i), negate)); break; }
					default: ASSERT(false, "Invalid constant type");
				}
			}
			return constant(0);
		}
	};

	intermediate propane_generator::parse(const char* file_path)
	{
		return intermediate_parser(file_path).finalize();
	}
}