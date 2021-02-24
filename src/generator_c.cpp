#include "propane_generator.hpp"
#include "generator.hpp"
#include "assembly_data.hpp"
#include "internal.hpp"
#include "errors.hpp"

#include <fstream>
using std::ofstream;

#define VALIDATE(errc, expr, fmt, ...) ENSURE(errc, expr, propane::generator_exception, fmt, __VA_ARGS__)

#define VALIDATE_ASSEMBLY(expr) VALIDATE(ERRC::GNR_INVALID_ASSEMBLY, expr, \
	"Attempted to generate from an invalid assembly")
#define VALIDATE_COMPATIBILITY(expr) VALIDATE(ERRC::GNR_INCOMPATIBLE_ASSEMBLY, expr, \
	"Attempted to generate from an assembly that was build using an incompatible toolchain")
#define VALIDATE_ENTRYPOINT(expr) VALIDATE(ERRC::GNR_ENTRYPOINT_NOT_FOUND, expr, \
	"Failed to find main entrypoint in assembly")

namespace propane
{
	class type_meta
	{
	public:
		bool is_resolved = false;
		string declaration;
		string generated;
		size_t ptr_offset = 0;
		size_t ptr_level = 0;
	};

	class method_meta
	{
	public:
		bool fwd_declared = false;
		bool is_declared = false;
		bool is_defined = false;
		unordered_set<method_idx> calls_made;
		unordered_set<global_idx> referenced_globals;
	};

	class global_meta
	{
	public:
		bool is_defined = false;
	};

	struct string_address_t
	{
		string_address_t() = default;
		string_address_t(const type* type, string_view addr) :
			type(type),
			addr(addr) {}

		const type* type = nullptr;
		string_view addr;
	};

	constexpr string_view operator_str[] =
	{
		" = ~",
		" = -",
		" *= ",
		" /= ",
		" %= ",
		" += ",
		" -= ",
		" <<= ",
		" >>= ",
		" &= ",
		" ^= ",
		" |= ",
	};
	inline constexpr bool is_unary(opcode op) noexcept
	{
		return op >= opcode::ari_not && op <= opcode::ari_neg;
	}

	constexpr string_view comparison_str[] =
	{
		" == ",
		" != ",
		" > ",
		" >= ",
		" < ",
		" <= ",
		" == 0",
		" != 0",
	};
	inline constexpr bool is_cmpzero(opcode op) noexcept
	{
		return op >= opcode::cze && op <= opcode::cnz;
	}

	inline constexpr type_idx get_comp_type(type_idx lhs, type_idx rhs) noexcept
	{
		if (lhs != rhs)
		{
			if (lhs == type_idx::f64 || rhs == type_idx::f64)
				return type_idx::f64;
			else if (lhs == type_idx::f32 || rhs == type_idx::f32)
				return type_idx::f32;
			else if (lhs <= type_idx::i32 && rhs <= type_idx::i32)
				return type_idx::i32;
			else if (lhs <= type_idx::i64 && rhs <= type_idx::i64)
				return type_idx::i64;
			else if (is_unsigned(lhs) == is_unsigned(rhs))
				return std::max(lhs, rhs);
			else
				return lhs;
		}
		else
		{
			return std::max(lhs, type_idx::i32);
		}
	}


	class generator_language_c final : ofstream
	{
	public:
		generator_language_c(const char* out_dir, const assembly_data& asm_data) :
			data(asm_data),
			database(asm_data.database),
			int_type(data.types[type_idx::i32]),
			offset_type(data.types[derive_type_index<offset_t>::value]),
			size_type(data.types[derive_type_index<size_t>::value]),
			vptr_type(data.types[type_idx::vptr])
		{
			string output_file = string(out_dir);
			if (!output_file.empty())
			{
				const char last = output_file.back();
				if (last != '/' && last != '\\') output_file.push_back('/');
			}
			output_file.append("main.c");

			this->open(output_file);
			ASSERT(*this, "Failed to open output path '%'", out_dir);

			// Reserve
			get_number_str(31);
			get_indent_str(7);

			type_meta.resize(data.types.size());
			method_meta.resize(data.methods.size());
			globals_meta.resize(data.globals.info.size());
			constants_meta.resize(data.constants.info.size());

			resolve_method(data.methods[data.main]);

			file_writer.write_str("#include \"propane.h\"");

			if (!type_definitions.empty())
			{
				file_writer.write_str(type_definitions);
			}
			if (!method_declarations.empty())
			{
				file_writer.write_str("\n");
				file_writer.write_str(method_declarations);
			}
			if (!constants.empty())
			{
				file_writer.write_str("\n");
				file_writer.write_str(constants);
			}
			if (!globals.empty())
			{
				file_writer.write_str("\n");
				file_writer.write_str(globals);
			}
			if (!method_definitions.empty())
			{
				file_writer.write_str(method_definitions);
			}

			write(file_writer.data(), std::streamsize(file_writer.size()));
		}


		type_meta& resolve_type(const type& type)
		{
			auto& meta = type_meta[type.index];
			if (meta.is_resolved) return meta;
			meta.is_resolved = true;

			const auto& underlying_type = type.is_array() ? resolve_type(type.generated.array.underlying_type) : meta;

			if (meta.declaration.empty())
			{
				resolve_name_recursive(type.index);
			}

			if (!is_base_type(type.index))
			{
				for (auto& field : type.fields)
				{
					resolve_type(field.type);
				}

				if (type.is_array() || !type.is_generated())
				{
					type_fields.clear();
					type_fields.write_str("\n\n");

					type_fields.write_str(meta.declaration);
					type_fields.write_str("\n{\n");
					if (type.is_array())
					{
						declare_array_field(type_fields, type.generated.array.underlying_type, type.generated.array.array_size);
					}
					else
					{
						for (size_t i = 0; i < type.fields.size(); i++)
						{
							if (i != 0) type_fields.write_str("\n");
							const auto& f = type.fields[i];
							declare_field(type_fields, database[f.name], f.type);
						}
					}
					type_fields.write_str("\n};");

					type_definitions.write_str(type_fields);
				}
			}

			return meta;
		}
		inline type_meta& resolve_type(type_idx type)
		{
			return resolve_type(data.types[type]);
		}

		method_meta& resolve_method(const method& m)
		{
			auto& meta = method_meta[m.index];
			if (meta.is_declared) return meta;
			meta.is_declared = true;

			if (!meta.is_defined && !m.is_internal())
			{
				const auto& signature = get_signature(m.signature);
				resolve_signature(signature);

				method_body.clear();
				method_frame.clear();

				stack_vars_used.clear();
				stack_vars_used.resize(m.stackvars.size());
				return_vars.clear();

				// Definition
				method_frame.write_str("\n\n");
				generate_method_declaration(method_frame, m, signature);
				method_frame.write_str("\n{\n");

				if (!m.bytecode.empty())
				{
					current_method = &m;
					current_signature = &signature;

					ret_idx = 0;
					return_type = type_idx::invalid;
					const auto& bc = m.bytecode;
					sf = stack_frame_t(bc.data(), bc.data() + bc.size(), bc.data(), 0, 0, 0, 0, 0, nullptr);

					label_idx = m.labels.size();
					label_queue.resize(label_idx);
					label_indices.clear();
					for (auto& label : m.labels)
					{
						label_queue[--label_idx] = label;
						label_indices.emplace(label, index_t(label_indices.size()));
					}

					evaluate();
				}

				method_frame.write_str(method_body);
				method_frame.write_str("}");

				if (!meta.calls_made.empty() || !meta.referenced_globals.empty())
				{
					string tmp = method_frame;

					// Declare called methods
					for (auto c : meta.calls_made)
					{
						const auto& call_method = get_method(c);
						auto& call_meta = resolve_method(call_method);
						if (!call_meta.is_defined && call_method.index != m.index)
						{
							declare_method(call_method);
						}
					}

					// Declare referenced constants
					for (auto g : meta.referenced_globals)
					{
						if (is_constant_flag_set(g))
						{
							const global_idx global_idx = g & global_flags::constant_mask;
							const auto& global_info = data.constants.info[global_idx];
							const auto& global_type = get_type(global_info.type);
							if (global_type.is_signature())
							{
								// Resolve method if signature
								const size_t method_handle = *reinterpret_cast<const size_t*>(data.constants.data.data() + global_info.offset);
								if (method_handle != 0)
								{
									const method_idx call_method_idx = method_idx(method_handle ^ size_t(data.internal_hash));
									ASSERT(data.methods.is_valid_index(call_method_idx), "Attempted to call an invalid method");
									auto& const_meta = resolve_method(call_method_idx);
									auto& const_call = get_method(call_method_idx);
									if (!const_meta.is_defined && call_method_idx != m.index)
									{
										declare_method(const_call);
									}
									if (global_info.name == const_call.name)
									{
										// Build-in method constant
										continue;
									}
								}
							}
						}
						resolve_global(g);
					}

					method_definitions.write_str(tmp);
				}
				else
				{
					method_definitions.write_str(method_frame);
				}
			}

			meta.is_defined = true;
			return meta;
		}
		inline method_meta& resolve_method(method_idx method)
		{
			return resolve_method(get_method(method));
		}

		global_meta& resolve_global(global_idx global)
		{
			const bool is_constant = is_constant_flag_set(global);
			auto& metas = is_constant ? constants_meta : globals_meta;
			global &= global_flags::constant_mask;

			ASSERT(metas.is_valid_index(global), "Global index out of range");
			auto& meta = metas[global];
			if (meta.is_defined) return meta;
			meta.is_defined = true;

			const auto& table = is_constant ? data.constants : data.globals;

			const auto& global_info = table.info[global];
			const auto& global_type = data.types[global_info.type];

			auto& dst_buf = is_constant ? constants : globals;

			const auto name_info = database[global_info.name];

			dst_buf.write_newline();
			const auto& type_meta = resolve_type(global_type);
			if (global_type.is_signature())
			{
				const size_t method_handle = *reinterpret_cast<const size_t*>(table.data.data() + global_info.offset);
				method_idx call_method_idx = method_idx(method_handle - 1);

				dst_buf.write_str(type_meta.declaration.substr(0, type_meta.ptr_offset));
				if (is_constant) dst_buf.write_strs("const ");
				dst_buf.write_strs("$", name_info);
				dst_buf.write_str(type_meta.declaration.substr(type_meta.ptr_offset));
			}
			else if (global_type.is_pointer())
			{
				dst_buf.write_strs(type_meta.declaration);
				if (is_constant) dst_buf.write_str(" const");
				dst_buf.write_strs(" $", name_info);
			}
			else
			{
				if (is_constant) dst_buf.write_strs("const ");
				dst_buf.write_strs(type_meta.declaration, " $", name_info);
			}

			dst_buf.write_str(" = ");
			if (global_type.is_pointer())
			{
				// If its a pointer type, we need to cast to dst to silence 'levels of indirection' warning
				dst_buf.write_str("(");
				dst_buf.write_strs(type_meta.declaration);
				if (is_constant) dst_buf.write_str(" const");
				dst_buf.write_str(")");
			}
			const_pointer_t addr = table.data.data() + global_info.offset;
			write_constant(dst_buf, addr, global_type.index, true);
			dst_buf.write_str(";");

			return meta;
		}

		void resolve_signature(const signature& signature)
		{
			for (auto& p : signature.parameters)
			{
				resolve_type(p.type);
			}
			resolve_type(signature.return_type);
		}


		void evaluate()
		{
			bool has_returned = false;
			while (true)
			{
				const size_t offset = size_t(sf.iptr - sf.ibeg);
				while (!label_queue.empty() && offset >= label_queue.back())
				{
					method_body.write_strs("$", get_number_str(label_idx), label_postfix, ":;\n");
					label_idx++;
					label_queue.pop_back();
				}

				if (sf.iptr == sf.iend)
				{
					ASSERT(!current_signature->has_return_value() || has_returned, "Function expects a return value");

					return;
				}

				has_returned = false;

				method_body.write_str(get_indent_str(1));
				instruction.clear();

				const opcode op = read_bytecode<opcode>(sf.iptr);
				switch (op)
				{
					case opcode::noop: noop(); break;

					case opcode::set: set(); break;
					case opcode::conv: conv(); break;

					case opcode::ari_not:
					case opcode::ari_neg:
					case opcode::ari_mul:
					case opcode::ari_div:
					case opcode::ari_mod:
					case opcode::ari_add:
					case opcode::ari_sub:
					case opcode::ari_lsh:
					case opcode::ari_rsh:
					case opcode::ari_and:
					case opcode::ari_xor:
					case opcode::ari_or: ari(op); break;

					case opcode::padd:
					case opcode::psub: ptr(op); break;
					case opcode::pdif: pdif(); break;

					case opcode::cmp:
					case opcode::ceq:
					case opcode::cne:
					case opcode::cgt:
					case opcode::cge:
					case opcode::clt:
					case opcode::cle:
					case opcode::cze:
					case opcode::cnz: cmp(op); break;

					case opcode::br: br(); break;

					case opcode::beq:
					case opcode::bne:
					case opcode::bgt:
					case opcode::bge:
					case opcode::blt:
					case opcode::ble:
					case opcode::bze:
					case opcode::bnz: br(op); break;

					case opcode::sw: sw(); break;

					case opcode::call: call(); break;
					case opcode::callv: callv(); break;
					case opcode::ret: has_returned = true; ret(); break;
					case opcode::retv: has_returned = true; retv(); break;

					case opcode::dump: dump(); break;

					default: ASSERT(false, "Malformed opcode: %", uint32_t(op));
				}

				method_body.write_str(instruction);
				method_body.write_str(";\n");
			}
		}


		void noop()
		{
			instruction.write_str("((void)0)");
		}

		void set()
		{
			auto sub = read_subcode();
			auto lhs_addr = read_address(false);
			auto rhs_addr = read_address(true);

			set(sub, lhs_addr, rhs_addr);
		}
		void set(subcode sub, string_address_t lhs_addr, string_address_t rhs_addr)
		{
			instruction.write_strs(lhs_addr.addr, " = ");
			if (lhs_addr.type != rhs_addr.type) write_cast(lhs_addr.type->index);
			instruction.write_str(rhs_addr.addr);
		}
		void conv()
		{
			auto sub = read_subcode();
			auto lhs_addr = read_address(false);
			auto rhs_addr = read_address(true);

			conv(sub, lhs_addr, rhs_addr);
		}
		void conv(subcode sub, string_address_t lhs_addr, string_address_t rhs_addr)
		{
			instruction.write_strs(lhs_addr.addr, " = ");
			if (lhs_addr.type != rhs_addr.type) write_cast(lhs_addr.type->index);
			instruction.write_str(rhs_addr.addr);
		}

		void ari(opcode op)
		{
			const bool unary = is_unary(op);
			auto sub = read_subcode();
			auto lhs_addr = read_address(true);
			auto rhs_addr = unary ? lhs_addr : read_address(true);

			const size_t op_idx = size_t(op - opcode::ari_not);

			if (unary)
			{
				instruction.write_strs(lhs_addr.addr, operator_str[op_idx], lhs_addr.addr);
			}
			else if (op == opcode::ari_mod && is_floating_point(lhs_addr.type->index))
			{
				const string_view mod_name = lhs_addr.type->index == type_idx::f32 ? "fmodf" : "fmod";
				instruction.write_strs(lhs_addr.addr, " = ", mod_name, "(", lhs_addr.addr, ", ");
				if (lhs_addr.type != rhs_addr.type)
				{
					write_cast(instruction, lhs_addr.type->index);
				}
				instruction.write_strs(rhs_addr.addr, ")");
			}
			else
			{
				instruction.write_strs(lhs_addr.addr, operator_str[op_idx]);
				if (lhs_addr.type != rhs_addr.type)
				{
					write_cast(instruction, lhs_addr.type->index);
				}
				instruction.write_strs(rhs_addr.addr);
			}
		}

		void ptr(opcode op)
		{
			auto sub = read_subcode();
			auto lhs_addr = read_address(true);
			auto rhs_addr = read_address(true);

			instruction.write_strs(lhs_addr.addr, op == opcode::padd ? " += " : " -= ", rhs_addr.addr);
		}
		void pdif()
		{
			auto lhs_addr = read_address(true);
			auto rhs_addr = read_address(true);

			// ((offset_t)(lhs) - (offset_t)(rhs)) / sizeof(underlying type)
			write_return_value(offset_type.index);
			instruction.write_str("(");
			write_cast(offset_type.index);
			instruction.write_str(lhs_addr.addr);
			instruction.write_str(" - ");
			write_cast(offset_type.index);
			instruction.write_str(rhs_addr.addr);
			instruction.write_str(") / ");
			write_cast(offset_type.index);
			instruction.write_strs("sizeof(", type_meta[lhs_addr.type->generated.pointer.underlying_type].declaration, ")");
		}

		void do_cmp(opcode op)
		{
			const bool cmpzero = is_cmpzero(op);
			auto sub = read_subcode();
			auto lhs_addr = read_address(true);
			auto rhs_addr = cmpzero ? lhs_addr : read_address(true);

			const size_t op_idx = size_t(op - opcode::ceq);

			if (cmpzero)
			{
				instruction.write_strs(lhs_addr.addr, comparison_str[op_idx]);
			}
			else
			{
				const type_idx cmp_type = get_comp_type(lhs_addr.type->index, rhs_addr.type->index);

				if (op == opcode::cmp)
				{
					if (lhs_addr.type->index != cmp_type) write_cast(instruction, cmp_type);
					instruction.write_strs(lhs_addr.addr, " < ");
					if (rhs_addr.type->index != cmp_type) write_cast(instruction, cmp_type);
					instruction.write_strs(rhs_addr.addr, " ? -1 : ");
					if (lhs_addr.type->index != cmp_type) write_cast(instruction, cmp_type);
					instruction.write_strs(lhs_addr.addr, " > ");
					if (rhs_addr.type->index != cmp_type) write_cast(instruction, cmp_type);
					instruction.write_strs(rhs_addr.addr, " ? 1 : 0");
				}
				else
				{
					if (lhs_addr.type->index != cmp_type) write_cast(instruction, cmp_type);
					instruction.write_strs(lhs_addr.addr, comparison_str[op_idx]);
					if (rhs_addr.type->index != cmp_type) write_cast(instruction, cmp_type);
					instruction.write_strs(rhs_addr.addr);
				}
			}
		}
		void cmp(opcode op)
		{
			write_return_value(type_idx::i32);
			do_cmp(op);
		}

		void br()
		{
			const size_t branch_location = read_bytecode<size_t>(sf.iptr);
			auto label_index = label_indices.find(branch_location);

			instruction.write_strs("goto $", get_number_str(size_t(label_index->second)), label_postfix);
		}
		void br(opcode op)
		{
			const size_t branch_location = read_bytecode<size_t>(sf.iptr);
			auto label_index = label_indices.find(branch_location);

			instruction.write_str("if (");
			do_cmp(op - (opcode::br - opcode::cmp));
			instruction.write_strs(") goto $", get_number_str(size_t(label_index->second)), label_postfix);
		}

		void sw()
		{
			string_address_t idx_addr = read_address(true);

			const uint32_t label_count = read_bytecode<uint32_t>(sf.iptr);

			const size_t* labels = reinterpret_cast<const size_t*>(sf.iptr);
			sf.iptr += sizeof(size_t) * label_count;

			instruction.write_strs("switch (", idx_addr.addr, ")\n\t{\n");
			for (uint32_t i = 0; i < label_count; i++)
			{
				auto label_index = label_indices.find(labels[i]);
				instruction.write_strs(get_indent_str(2), "case ", get_number_str(i), ": goto $", get_number_str(size_t(label_index->second)), label_postfix, ";\n");
			}
			instruction.write_str("\t}");
		}

		void call()
		{
			const method_idx call_idx = read_bytecode<method_idx>(sf.iptr);
			method_meta[current_method->index].calls_made.emplace(call_idx);

			const auto& method = get_method(call_idx);
			const auto& signature = get_signature(method.signature);

			write_return_value(signature.return_type);

			instruction.write_strs("$", database[method.name]);

			write_param(signature);
		}
		void callv()
		{
			auto method_ptr = read_address(true);

			const auto& signature = get_signature(method_ptr.type->generated.signature.index);

			write_return_value(signature.return_type);

			if (method_ptr.addr.starts_with('*'))
			{
				instruction.write_strs("(", method_ptr.addr, ")");
			}
			else
			{
				instruction.write_str(method_ptr.addr);
			}

			write_param(signature);
		}
		void write_param(const signature& signature)
		{
			const size_t arg_count = size_t(read_bytecode<uint8_t>(sf.iptr));

			instruction.write_str('(');
			for (size_t i = 0; i < signature.parameters.size(); i++)
			{
				auto sub = read_subcode();
				if (i > 0) instruction.write_str(", ");
				instruction.write_str(read_address(true).addr);
			}
			instruction.write_str(')');
		}

		void ret()
		{
			instruction.write_str("return");
		}
		void retv()
		{
			auto sub = read_subcode();
			auto ret_value = read_address(true);

			instruction.write_str("return ");
			if (current_signature->return_type != ret_value.type->index)
			{
				write_cast(current_signature->return_type);
			}
			instruction.write_str(ret_value.addr);
		}

		void dump()
		{
			auto src_addr = read_address(true);
			auto& operand = get_next_buffer();
			operand.push_back('(');
			operand.append(src_addr.addr);
			operand.push_back(')');

			auto& fmt = get_next_buffer();
			auto& arg = get_next_buffer();

			dump_recursive(*src_addr.type, fmt, arg, operand);

			instruction.write_strs("printf(\"", fmt, "\\n\"", arg, ")");
		}
		vector<string_writer> dump_name_generators;
		string_writer name_buf;
		void dump_recursive(const type& type, string_writer& fmt, string_writer& arg, string_writer& addr)
		{
			name_buf.clear();
			if (type.name != name_idx::invalid)
			{
				name_buf.append(database[type.name]);
			}
			else
			{
				data.generate_name(type.index, name_buf);
			}
			fmt.write_str(name_buf);

			if (type.index < type_idx::f64)
			{
				switch (type.index)
				{
					case type_idx::i8: fmt.write_str("(%hhi)"); break;
					case type_idx::u8: fmt.write_str("(%hhu)"); break;
					case type_idx::i16: fmt.write_str("(%hi)"); break;
					case type_idx::u16: fmt.write_str("(%hu)"); break;
					case type_idx::i32: fmt.write_str("(%i)"); break;
					case type_idx::u32: fmt.write_str("(%u)"); break;
					case type_idx::i64: fmt.write_str("(%lli)"); break;
					case type_idx::u64: fmt.write_str("(%llu)"); break;
					case type_idx::f32:
					case type_idx::f64: fmt.write_str("(%g)"); break;
				}

				arg.write_str(", ");
				arg.write_str(addr);
			}
			else
			{
				if (type.is_pointer() || type.is_signature())
				{
					fmt.write_str("(%p)");

					arg.write_str(", ");
					arg.write_str("(void*)");
					arg.write_str(addr);
				}
				else if (type.is_array())
				{
					//fmt.write_str("{");
					//const auto& underlying_type = get_type(type.generated.array.underlying_type);
					//for (size_t i = 0; i < type.generated.array.array_size; i++)
					//{
					//	fmt.write_str(i == 0 ? " " : ", ");
					//
					//	const size_t s = addr.size();
					//	addr.write_str(".$val[");
					//	addr.write_str(get_number_str(i));
					//	addr.write_str("]");
					//	dump_recursive(underlying_type, fmt, arg, addr);
					//	addr.resize(s);
					//}
					//fmt.write_str(" }");
				}
				else if (!type.fields.empty())
				{
					fmt.write_str("{");
					for (size_t i = 0; i < type.fields.size(); i++)
					{
						auto& field = type.fields[i];
						fmt.write_str(i == 0 ? " " : ", ");
						fmt.write_str(database[field.name]);
						fmt.write_str(" = ");

						const size_t s = addr.size();
						addr.write_str(".");
						addr.write_strs("$", database[field.name]);
						dump_recursive(get_type(field.type), fmt, arg, addr);
						addr.resize(s);
					}
					fmt.write_str(" }");
				}
				else
				{
					fmt.write_str("(?)");
				}
			}
		}


		type_meta& resolve_name_recursive(type_idx t)
		{
			const auto& type = get_type(t);
			auto& meta = type_meta[type.index];
			if (meta.declaration.empty())
			{
				if (!type.is_generated())
				{
					if (!is_base_type(type.index))
					{
						meta.generated = "$";
						meta.generated.append(database[type.name]);
						meta.declaration = type.is_union() ? "union " : "struct ";
						meta.declaration.append(meta.generated);
					}
					else
					{
						switch (type.index)
						{
							case type_idx::i8: meta.declaration = "int8_t"; break;
							case type_idx::u8: meta.declaration = "uint8_t"; break;
							case type_idx::i16: meta.declaration = "int16_t"; break;
							case type_idx::u16: meta.declaration = "uint16_t"; break;
							case type_idx::i32: meta.declaration = "int32_t"; break;
							case type_idx::u32: meta.declaration = "uint32_t"; break;
							case type_idx::i64: meta.declaration = "int64_t"; break;
							case type_idx::u64: meta.declaration = "uint64_t"; break;
							case type_idx::f32: meta.declaration = "float"; break;
							case type_idx::f64: meta.declaration = "double"; break;
							case type_idx::vptr: meta.declaration = "void"; break;
							case type_idx::voidtype: meta.declaration = "void"; break;
						}
						meta.generated = "$" + meta.declaration;
						if (type.index == type_idx::vptr)
						{
							meta.declaration.push_back('*');
							meta.generated.append("$P1");
						}
					}
				}
				else
				{
					if (type.is_pointer())
					{
						const auto& underlying_type = get_type(type.generated.pointer.underlying_type);
						const auto& underlying_meta = resolve_name_recursive(underlying_type.index);

						if (underlying_meta.ptr_offset != 0)
						{
							meta.declaration.append(underlying_meta.declaration.substr(0, underlying_meta.ptr_offset));
							meta.declaration.append("*");
							meta.declaration.append(underlying_meta.declaration.substr(underlying_meta.ptr_offset));
							meta.ptr_offset = underlying_meta.ptr_offset + 1;
						}
						else
						{
							meta.declaration.append(underlying_meta.declaration);
							meta.declaration.append("*");
						}

						if (underlying_type.is_pointer())
						{
							meta.ptr_level = underlying_meta.ptr_level + 1;
							meta.generated.append(underlying_meta.generated.substr(0, underlying_meta.generated.find_last_of('$')));
							meta.generated.append("$P");
							meta.generated.append(get_number_str(meta.ptr_level));
						}
						else
						{
							meta.ptr_level = 1;
							meta.generated.append(underlying_meta.generated);
							meta.generated.append("$P1");
						}
					}
					else if (type.is_array())
					{
						const auto& underlying_type = get_type(type.generated.pointer.underlying_type);
						const auto& underlying_meta = resolve_name_recursive(underlying_type.index);

						meta.generated = underlying_meta.generated;
						meta.generated.append("$A");
						meta.generated.append(std::to_string(type.generated.array.array_size));

						meta.declaration = "struct ";
						meta.declaration.append(meta.generated);
					}
					else if (type.is_signature())
					{
						meta.generated = "$";

						const auto& signature = get_signature(type.generated.signature.index);
						const auto& return_type = get_type(signature.return_type);
						const auto& return_type_meta = resolve_name_recursive(return_type.index);
						if (return_type_meta.ptr_offset != 0)
						{
							meta.declaration.append(return_type_meta.declaration.substr(0, return_type_meta.ptr_offset));
						}
						else
						{
							meta.declaration.append(return_type_meta.declaration);
						}

						meta.generated.append(return_type_meta.generated);

						meta.ptr_offset = meta.declaration.size() + 2;
						meta.declaration.append("(*)(");
						for (size_t i = 0; i < signature.parameters.size(); i++)
						{
							if (i > 0) meta.declaration.append(", ");
							const auto& param_type = get_type(signature.parameters[i].type);
							const auto& param_type_meta = resolve_name_recursive(param_type.index);
							meta.declaration.append(param_type_meta.declaration);

							meta.generated.append(param_type_meta.generated);
						}
						meta.declaration.append(")");

						if (return_type_meta.ptr_offset != 0)
						{
							meta.declaration.append(return_type_meta.declaration.substr(return_type_meta.ptr_offset));
						}
					}
					else
					{
						meta.generated = meta.declaration = "<???>";
					}
				}
			}
			return meta;
		}


		// Assembly data
		const assembly_data& data;
		const string_table<name_idx>& database;

		inline const type& get_type(type_idx type) const
		{
			return data.types[type];
		}
		inline const method& get_method(method_idx method) const
		{
			return data.methods[method];
		}
		inline const signature& get_signature(signature_idx signature) const
		{
			return data.signatures[signature];
		}

		// Stack frame
		const method* current_method = nullptr;
		const signature* current_signature = nullptr;
		vector<size_t> label_queue;
		unordered_map<size_t, index_t> label_indices;
		size_t label_idx = 0;
		size_t ret_idx = 0;
		type_idx return_type = type_idx::invalid;
		stack_frame_t sf;

		// String buffers
		static constexpr string_view stack_postfix = "s";
		static constexpr string_view param_postfix = "p";
		static constexpr string_view retval_postfix = "r";
		static constexpr string_view label_postfix = "l";

		vector<bool> stack_vars_used;
		vector<type_idx> return_vars;
		string_writer type_fields;
		string_writer type_definitions;
		string_writer constants;
		string_writer globals;
		string_writer method_frame;
		string_writer method_body;
		string_writer instruction;
		string_writer method_declarations;
		string_writer method_definitions;
		string_writer file_writer;

		string_writer string_buffers[4];
		size_t buffer_index = 0;
		string_writer& get_next_buffer()
		{
			string_writer& buf = string_buffers[buffer_index];
			buf.clear();
			buffer_index = (buffer_index + 1) & 3;
			return buf;
		}

		string generate_name;

		// Meta
		indexed_vector<type_idx, type_meta> type_meta;
		indexed_vector<method_idx, method_meta> method_meta;
		indexed_vector<global_idx, global_meta> globals_meta;
		indexed_vector<global_idx, global_meta> constants_meta;

		indexed_vector<size_t, string> number_str;
		inline string_view get_number_str(size_t idx)
		{
			if (idx >= number_str.size())
			{
				const size_t beg = number_str.size();
				number_str.resize(idx + 1);
				for (size_t i = beg; i < number_str.size(); i++)
				{
					number_str[i] = std::to_string(i);
				}
			}
			return number_str[idx];
		}
		indexed_vector<size_t, string> indent_str;
		inline string_view get_indent_str(size_t idx)
		{
			if (idx >= indent_str.size())
			{
				const size_t beg = indent_str.empty() ? 1 : indent_str.size();
				indent_str.resize(idx + 1);
				for (size_t i = beg; i < indent_str.size(); i++)
				{
					indent_str[i] = indent_str[i - 1];
					indent_str[i].push_back('\t');
				}
			}
			return indent_str[idx];
		}

		void write_literal(string_writer& buf, const_pointer_t ptr, type_idx type)
		{
			switch (type)
			{
				case type_idx::i8: buf.write_str(std::to_string(*reinterpret_cast<const i8*>(ptr))); break;
				case type_idx::u8: buf.write_str(std::to_string(*reinterpret_cast<const u8*>(ptr))); break;
				case type_idx::i16: buf.write_str(std::to_string(*reinterpret_cast<const i16*>(ptr))); break;
				case type_idx::u16: buf.write_str(std::to_string(*reinterpret_cast<const u16*>(ptr))); break;
				case type_idx::i32: buf.write_str(std::to_string(*reinterpret_cast<const i32*>(ptr))); break;
				case type_idx::u32: buf.write_str(std::to_string(*reinterpret_cast<const u32*>(ptr))); break;
				case type_idx::i64: buf.write_str(std::to_string(*reinterpret_cast<const i64*>(ptr))); break;
				case type_idx::u64: buf.write_str(std::to_string(*reinterpret_cast<const u64*>(ptr))); break;
				case type_idx::f32: buf.write_strs(std::to_string(*reinterpret_cast<const f32*>(ptr)), "f"); break;
				case type_idx::f64: buf.write_str(std::to_string(*reinterpret_cast<const f64*>(ptr))); break;
				case type_idx::vptr: write_hex(buf, *reinterpret_cast<const size_t*>(ptr)); break;
				default: ASSERT(false, "Unknown constant type");
			}
		}
		void write_hex(string_writer& buf, size_t value)
		{
			buf.write_str("0x");
			constexpr size_t nibble_count = sizeof(size_t) * 2;
			for (size_t i = 0; i < nibble_count; i++)
			{
				const size_t nibble = (value >> ((nibble_count - 1) * 4)) & size_t(0xF);
				if (nibble < 10)
				{
					buf.write_str('0' + char(nibble));
				}
				else
				{
					buf.write_str('A' + char(nibble - 10));
				}
				value <<= 4;
			}
		}
		void write_constant(string_writer& buf, const_pointer_t& ptr, type_idx type, bool top_level)
		{
			const auto& t = data.types[type];

			if (t.is_pointer())
			{
				write_hex(buf, *reinterpret_cast<const size_t*>(ptr));
				ptr += get_base_type_size(type_idx::vptr);
			}
			else if (t.is_arithmetic())
			{
				write_literal(buf, ptr, type);
				ptr += get_base_type_size(type);
			}
			else if (t.is_signature())
			{
				size_t method_handle = *reinterpret_cast<const size_t*&>(ptr)++;
				if (method_handle == 0)
				{
					buf.write_strs("0");
				}
				else
				{
					const method_idx call_idx = method_idx(method_handle ^ size_t(data.internal_hash));
					ASSERT(data.methods.is_valid_index(call_idx), "Invalid method index");

					const auto& call_method = get_method(call_idx);
					declare_method(call_method);
					resolve_method(call_method);

					buf.write_strs("$", database[call_method.name]);
				}
			}
			else if (t.is_array())
			{
				if (top_level) buf.write_str("{ ");
				for (size_t i = 0; i < t.generated.array.array_size; i++)
				{
					if (i != 0) buf.write_str(", ");
					write_constant(buf, ptr, t.generated.array.underlying_type, false);
				}
				if (top_level) buf.write_str(" }");
			}
			else
			{
				if (top_level) buf.write_str("{ ");
				for (size_t i = 0; i < t.fields.size(); i++)
				{
					if (i != 0) buf.write_str(", ");
					write_constant(buf, ptr, t.fields[i].type, false);
				}
				if (top_level) buf.write_str(" }");
			}
		}

		inline subcode read_subcode() noexcept
		{
			return read_bytecode<subcode>(sf.iptr);
		}
		string_address_t read_address(bool is_rhs)
		{
			string_writer& buf = get_next_buffer();

			string_address_t result;

			const address_data_t& addr = *reinterpret_cast<const address_data_t*>(sf.iptr);

			const auto& minf = *current_method;
			const auto& csig = *current_signature;

			bool is_constant = false;

			switch (addr.header.prefix())
			{
				case address_prefix::indirection: buf.write_str("*"); break;
				case address_prefix::address_of: buf.write_str("&"); break;
				case address_prefix::size_of: buf.write_str("sizeof("); break;
			}

			const index_t index = addr.header.index();
			type_idx sv_type = type_idx::invalid;
			switch (addr.header.type())
			{
				case address_type::stackvar:
				{
					if (size_t(index) == minf.stackvars.size())
					{
						ASSERT(has_return_value(), "Return value address has not been set");

						ASSERT(return_type != type_idx::invalid, "Return value address has not been set");

						buf.write_strs("$", get_number_str(size_t(ret_idx)), retval_postfix);

						result.type = &get_type(return_type);
					}
					else
					{
						ASSERT(index < minf.stackvars.size(), "Stack index out of range");

						const auto& stack_var = minf.stackvars[index];

						buf.write_strs("$", get_number_str(size_t(index)), stack_postfix);

						result.type = &get_type(stack_var.type);
						sv_type = stack_var.type;
					}
				}
				break;

				case address_type::parameter:
				{
					ASSERT(index < csig.parameters.size(), "Parameter index out of range");

					buf.write_strs("$", get_number_str(size_t(index)), param_postfix);

					const auto& param = csig.parameters[index];
					result.type = &get_type(param.type);
				}
				break;

				case address_type::global:
				{
					global_idx global = (global_idx)index;

					method_meta[current_method->index].referenced_globals.emplace(global);

					is_constant = is_constant_flag_set(global);
					const auto& table = is_constant ? data.constants : data.globals;
					global &= global_flags::constant_mask;

					buf.write_strs("$", database[table.info[global].name]);

					const auto& global_info = table.info[global];
					result.type = &get_type(global_info.type);
				}
				break;

				case address_type::constant:
				{
					ASSERT(is_rhs, "Constant cannot be a left-hand side operand");
					const type_idx btype_idx = type_idx(index);
					ASSERT(btype_idx <= type_idx::vptr, "Malformed constant opcode");
					sf.iptr += sizeof(address_header);
					pointer_t ptr = (pointer_t)sf.iptr;
					const auto& type = get_type(btype_idx);
					string_writer& buf = get_next_buffer();
					write_literal(buf, ptr, type.index);
					sf.iptr += type.total_size;
					return string_address_t(&type, buf);
				}
				break;
			}

			switch (addr.header.modifier())
			{
				case address_modifier::none: break;

				case address_modifier::direct_field:
				{
					const auto& field = data.offsets[addr.field];

					const auto& type = *result.type;
					ASSERT(!type.is_pointer(), "Attempted to deref a field on a non-pointer type");
					ASSERT(type.index == field.name.parent_type, "Field type mismatch");

					for (auto& it : field.name.field_names)
					{
						buf.write_str(".");
						buf.write_strs("$", database[it]);
					}

					result.type = &get_type(field.type);
				}
				break;

				case address_modifier::indirect_field:
				{
					const auto& field = data.offsets[addr.field];

					const auto& type = *result.type;
					ASSERT(type.is_pointer(), "Attempted to dereference a non-pointer type");
					const auto& underlying_type = get_type(type.generated.pointer.underlying_type);
					ASSERT(underlying_type.index == field.name.parent_type, "Field type mismatch");

					bool first = true;
					for (auto& it : field.name.field_names)
					{
						buf.write_str(first ? "->" : ".");
						buf.write_strs("$", database[it]);
						first = false;
					}

					result.type = &data.types[field.type];
				}
				break;

				case address_modifier::subscript:
				{
					const offset_t offset = addr.offset;

					const auto& type = *result.type;
					if (type.is_pointer())
					{
						result.type = &get_type(type.generated.pointer.underlying_type);
					}
					else if (type.is_array())
					{
						buf.write_str(".$val");
						result.type = &get_type(type.generated.array.underlying_type);
					}
					else
					{
						ASSERT(false, "Offset is not valid here");
					}
					buf.write_str("[");
					buf.write_str(std::to_string(offset));
					buf.write_str("]");
				}
				break;
			}

			switch (addr.header.prefix())
			{
				case address_prefix::none: break;

				case address_prefix::indirection:
				{
					const auto& type = *result.type;
					ASSERT(type.is_pointer(), "Attempted to dereference a non-pointer type");
					ASSERT(type.index != type_idx::vptr, "Attempted to dereference an abstract pointer type");

					result.type = &get_type(type.generated.pointer.underlying_type);
				}
				break;

				case address_prefix::address_of:
				{
					const type_idx dst_type = result.type->pointer_type;
					result.type = dst_type == type_idx::invalid ? &vptr_type : &get_type(dst_type);

					// Cast away constness
					if (is_constant)
					{
						string tmp = buf;
						buf.clear();
						write_cast(buf, dst_type);
						buf.append(tmp);
					}
				}
				break;

				case address_prefix::size_of:
				{
					result.type = &size_type;

					buf.write_str(')');
				}
				break;
			}

			if (addr.header.type() == address_type::stackvar)
			{
				if (size_t(index) < stack_vars_used.size() && !stack_vars_used[size_t(index)])
				{
					if (addr.header.prefix() == address_prefix::none && addr.header.modifier() == address_modifier::none && !is_rhs)
					{
						buf.clear();
						declare_stackvar(buf, stack_postfix, size_t(index), sv_type);
					}
					else
					{
						declare_stackvar(method_body, stack_postfix, size_t(index), sv_type);
						method_body.write_strs(";\n", get_indent_str(1));
					}

					stack_vars_used[size_t(index)] = true;
				}
			}

			sf.iptr += sizeof(address_data_t);

			result.addr = buf;
			return result;
		}
		
		void declare_method(const method& method)
		{
			if (!method_meta[method.index].fwd_declared)
			{
				method_declarations.write_newline();
				const auto& signature = get_signature(method.signature);
				resolve_signature(signature);
				generate_method_declaration(method_declarations, method, signature);
				method_declarations.write_str(";");
				method_meta[method.index].fwd_declared = true;
			}
		}
		void generate_method_declaration(string_writer& dst, const method& method, const signature& signature)
		{
			const auto& return_type = get_type(signature.return_type);
			const auto& return_meta = type_meta[signature.return_type];
			if (return_meta.ptr_offset != 0)
			{
				dst.write_str(return_meta.declaration.substr(0, return_meta.ptr_offset));
			}
			else
			{
				dst.write_strs(return_meta.declaration, " ");
			}

			dst.write_strs("$", database[method.name], "(");
			for (size_t i = 0; i < signature.parameters.size(); i++)
			{
				if (i > 0) dst.write_str(", ");
				const type_idx param_type = signature.parameters[i].type;
				declare_stackvar(dst, param_postfix, i, param_type);
			}
			dst.write_str(')');

			if (return_meta.ptr_offset != 0)
			{
				dst.write_str(return_meta.declaration.substr(return_meta.ptr_offset));
			}
		}

		void declare_stackvar(string_writer& dst, string_view postfix, size_t idx, type_idx type)
		{
			const auto& meta = resolve_type(type);
			if (meta.ptr_offset != 0)
			{
				dst.write_str(meta.declaration.substr(0, meta.ptr_offset));
				dst.write_strs("$", get_number_str(idx), postfix);
				dst.write_str(meta.declaration.substr(meta.ptr_offset));
			}
			else
			{
				dst.write_strs(meta.declaration, " $", get_number_str(idx), postfix);
			}
		}
		void declare_field(string_writer& dst, string_view name, type_idx type)
		{
			dst.write_str(get_indent_str(1));
			const auto& meta = resolve_type(type);
			if (meta.ptr_offset != 0)
			{
				dst.write_str(meta.declaration.substr(0, meta.ptr_offset));
				dst.write_strs("$", name);
				dst.write_str(meta.declaration.substr(meta.ptr_offset));
			}
			else
			{
				dst.write_strs(meta.declaration, " $", name);
			}
			dst.write_str(";");
		}
		void declare_array_field(string_writer& dst, type_idx type, size_t array_size)
		{
			dst.write_str(get_indent_str(1));
			const auto& meta = resolve_type(type);
			if (meta.ptr_offset != 0)
			{
				dst.write_str(meta.declaration.substr(0, meta.ptr_offset));
				dst.write_strs("$val", "[", std::to_string(array_size), "]");
				dst.write_str(meta.declaration.substr(meta.ptr_offset));
			}
			else
			{
				dst.write_strs(meta.declaration, " $val", "[", std::to_string(array_size), "]");
			}
			dst.write_str(";");
		}

		void write_return_value(type_idx type)
		{
			if (type == type_idx::voidtype)
			{
				ret_idx = 0;
				return_type = type_idx::invalid;
				return;
			}

			return_type = type;

			for (size_t i = 0; i < return_vars.size(); i++)
			{
				if (return_vars[i] == type)
				{
					ret_idx = i;
					instruction.write_strs("$", get_number_str(ret_idx), retval_postfix, " = ");
					return;
				}
			}

			ret_idx = return_vars.size();
			declare_stackvar(instruction, retval_postfix, ret_idx, type);
			instruction.write_str(" = ");
			return_vars.push_back(type);
		}
		bool has_return_value()
		{
			return return_type != type_idx::invalid;
		}

		void write_cast(string_writer& dst, type_idx dst_type)
		{
			dst.write_strs("(", resolve_name_recursive(dst_type).declaration, ")");
		}
		void write_cast(type_idx dst_type)
		{
			write_cast(instruction, dst_type);
		}

		// Type constant
		const type& int_type;
		const type& offset_type;
		const type& size_type;
		const type& vptr_type;
	};

	void c_generator::generate(const char* out_dir, const assembly& linked_assembly)
	{
		VALIDATE_ASSEMBLY(linked_assembly.is_valid());
		VALIDATE_COMPATIBILITY(linked_assembly.is_compatible());

		const assembly_data& data = linked_assembly.assembly_ref();
		VALIDATE_ENTRYPOINT(data.methods.is_valid_index(data.main));

		generator_language_c generator(out_dir, data);
	}
}