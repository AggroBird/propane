#include "propane_translator.hpp"
#include "generation.hpp"
#include "utility.hpp"
#include "assembly_data.hpp"
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
#define VALIDATE_FILE_OPEN(expr, file_path) VALIDATE(ERRC::GNR_FILE_EXCEPTION, expr, \
	"Failed to open output file: \"%\"", file_path)

namespace propane
{
	class translator_propane_impl final : ofstream
	{
	public:
		translator_propane_impl(const char* out_file, const assembly_data& asm_data) :
			data(asm_data),
			database(asm_data.database),
			int_type(data.types[type_idx::i32]),
			offset_type(data.types[derive_type_index<offset_t>::value]),
			size_type(data.types[derive_type_index<size_t>::value]),
			vptr_type(data.types[type_idx::vptr])
		{
			string output_file = string(out_file);
			if (!output_file.empty())
			{
				const char last = output_file.back();
				if (last != '/' && last != '\\') output_file.push_back('/');
			}

			this->open(output_file);
			VALIDATE_FILE_OPEN(this->operator bool(), out_file);

			type_names.resize(data.types.size());

			// Types
			write_types();

			// Globals
			write_globals(true);
			write_globals(false);

			// Methods
			write_methods();

			write(file_writer.data(), std::streamsize(file_writer.size()));
		}


		string_writer file_writer;

		void write_types()
		{
			for (auto& t : data.types)
			{
				if (is_base_type(t.index)) continue;
				if (t.is_generated()) continue;

				file_writer.write_strs(t.is_union() ? "union " : "struct ", resolve_type_name(t));
				file_writer.write_newline();
				for (auto& f : t.fields)
				{
					file_writer.write_strs("\t", resolve_type_name(f.type), " ", database[f.name], "\n");
				}
				file_writer.write_strs("end\n\n");
			}
		}
		void write_globals(bool constants)
		{
			const auto& table = constants ? data.constants : data.globals;
			if (table.info.empty()) return;

			file_writer.write_strs(constants ? "constant" : "global", "\n");
			for (auto& g : table.info)
			{
				file_writer.write_strs("\t", resolve_type_name(g.type), " ", database[g.name]);
				const_pointer_t addr = table.data.data() + g.offset;
				write_constant(addr, g.type, true);
				file_writer.write_newline();
			}
			file_writer.write_str("end\n\n");
		}
		void write_methods()
		{
			for (auto& m : data.methods)
			{
				if (m.is_internal()) continue;

				const auto& signature = data.signatures[m.signature];
				file_writer.write_strs("method ", database[m.name]);
				if (signature.has_return_value()) file_writer.write_strs(" returns ", resolve_type_name(signature.return_type));
				if (!signature.parameters.empty())
				{
					file_writer.write_strs(" parameters\n");
					for (size_t i = 0; i < signature.parameters.size(); i++)
					{
						const auto& p = signature.parameters[i];
						file_writer.write_strs("\t\t", get_number_str(i), ": ", resolve_type_name(p.type), "\n");
					}
					file_writer.write_str("\tend\n\n");
				}
				else
				{
					file_writer.write_newline();
				}

				if (!m.stackvars.empty())
				{
					file_writer.write_str("\tstack\n");
					for (size_t i = 0; i < m.stackvars.size(); i++)
					{
						const auto& sv = m.stackvars[i];
						file_writer.write_strs("\t\t", get_number_str(i), ": ", resolve_type_name(sv.type), "\n");
					}
					file_writer.write_str("\tend\n\n");
				}

				// Evaluate bytecode
				sv_count = m.stackvars.size();
				label_idx = m.labels.size();
				label_queue.resize(label_idx);
				label_indices.clear();
				for (auto& label : m.labels)
				{
					label_queue[--label_idx] = label;
					label_indices.emplace(label, index_t(label_indices.size()));
				}

				const auto& bc = m.bytecode;
				sf = stack_frame_t(bc.data(), bc.data() + bc.size(), bc.data(), 0, 0, 0, 0, 0, nullptr);
				evaluate();

				file_writer.write_strs("end\n\n");
			}
		}


		void evaluate()
		{
			while (true)
			{
				const size_t offset = size_t(sf.iptr - sf.ibeg);
				while (!label_queue.empty() && offset >= label_queue.back())
				{
					file_writer.write_strs("label_", get_number_str(label_idx), ":\n");
					label_idx++;
					label_queue.pop_back();
				}

				if (sf.iptr >= sf.iend)
				{
					return;
				}

				const opcode op = read_bytecode<opcode>(sf.iptr);
				file_writer.write_strs("\t", opcode_str(op));
				switch (op)
				{
					case opcode::noop: break;
					case opcode::ret: break;

					case opcode::dump:
					{
						read_address();
					}
					break;

					case opcode::pdif:
					{
						read_address();
						read_address();
					}
					break;

					case opcode::ari_not:
					case opcode::ari_neg:
					case opcode::cze:
					case opcode::cnz:
					case opcode::retv:
					{
						read_subcode();
						read_address();
					}
					break;

					case opcode::set:
					case opcode::conv:
					case opcode::ari_mul:
					case opcode::ari_div:
					case opcode::ari_mod:
					case opcode::ari_add:
					case opcode::ari_sub:
					case opcode::ari_lsh:
					case opcode::ari_rsh:
					case opcode::ari_and:
					case opcode::ari_xor:
					case opcode::ari_or:
					case opcode::padd:
					case opcode::psub:
					case opcode::cmp:
					case opcode::ceq:
					case opcode::cne:
					case opcode::cgt:
					case opcode::cge:
					case opcode::clt:
					case opcode::cle:
					{
						read_subcode();
						read_address();
						read_address();
					}
					break;

					case opcode::br:
					{
						read_label();
					}
					break;

					case opcode::bze:
					case opcode::bnz:
					{
						read_label();
						read_subcode();
						read_address();
					}
					break;

					case opcode::beq:
					case opcode::bne:
					case opcode::bgt:
					case opcode::bge:
					case opcode::blt:
					case opcode::ble:
					{
						read_label();
						read_subcode();
						read_address();
						read_address();
					}
					break;

					case opcode::sw:
					{
						read_address();
						const uint32_t label_count = read_bytecode<uint32_t>(sf.iptr);

						for (uint32_t i = 0; i < label_count; i++)
						{
							read_label();
						}
					}
					break;

					case opcode::call:
					{
						const method_idx idx = read_bytecode<method_idx>(sf.iptr);
						file_writer.write_strs(" ", database[data.methods[idx].name]);
						const size_t arg_count = size_t(read_bytecode<uint8_t>(sf.iptr));
						for (size_t i = 0; i < arg_count; i++)
						{
							read_subcode();
							read_address();
						}
					}
					break;

					case opcode::callv:
					{
						read_address();
						const size_t arg_count = read_bytecode<uint8_t>(sf.iptr);
						for (size_t i = 0; i < arg_count; i++)
						{
							read_subcode();
							read_address();
						}
					}
					break;


					default: ASSERT(false, "Malformed opcode: %", uint32_t(op));
				}
				file_writer.write_newline();
			}
		}

		size_t sv_count = 0;
		vector<size_t> label_queue;
		unordered_map<size_t, index_t> label_indices;
		size_t label_idx = 0;
		stack_frame_t sf;


		inline subcode read_subcode() noexcept
		{
			return read_bytecode<subcode>(sf.iptr);
		}
		void read_address()
		{
			const address_data_t& addr = *reinterpret_cast<const address_data_t*>(sf.iptr);

			file_writer.write_space();

			switch (addr.header.prefix())
			{
				case address_prefix::indirection: file_writer.write_str("*"); break;
				case address_prefix::address_of: file_writer.write_str("&"); break;
				case address_prefix::size_of: file_writer.write_str("!"); break;
			}

			const index_t index = addr.header.index();
			switch (addr.header.type())
			{
				case address_type::stackvar:
				{
					if (index == sv_count)
					{
						file_writer.write_str("{^}");
					}
					else
					{
						file_writer.write_strs("{", get_number_str(index), "}");
					}
				}
				break;

				case address_type::parameter:
				{
					file_writer.write_strs("(", get_number_str(index), ")");
				}
				break;

				case address_type::global:
				{
					global_idx global = (global_idx)index;

					const bool is_constant = is_constant_flag_set(global);
					const auto& table = is_constant ? data.constants : data.globals;
					global &= global_flags::constant_mask;

					file_writer.write_str(database[table.info[global].name]);
				}
				break;

				case address_type::constant:
				{
					const type_idx btype_idx = type_idx(index);
					sf.iptr += sizeof(address_header);
					pointer_t ptr = (pointer_t)sf.iptr;
					const auto& type = data.types[btype_idx];
					write_literal(ptr, type.index);
					sf.iptr += type.total_size;
					return;
				}
				break;
			}

			switch (addr.header.modifier())
			{
				case address_modifier::direct_field:
				{
					file_writer.write_str(".");
					write_offset(addr.field);
				}
				break;

				case address_modifier::indirect_field:
				{
					file_writer.write_str("->");
					write_offset(addr.field);
				}
				break;

				case address_modifier::subscript:
				{
					file_writer.write_strs("[", std::to_string(addr.offset), "]");
				}
				break;
			}

			sf.iptr += sizeof(address_data_t);
		}
		void read_label()
		{
			const size_t jump = read_bytecode<size_t>(sf.iptr);
			auto find = label_indices.find(jump);
			if (find != label_indices.end())
			{
				file_writer.write_strs(" label_", get_number_str(find->second));
			}
		}

		void write_literal(const_pointer_t ptr, type_idx type)
		{
			switch (type)
			{
				case type_idx::i8: file_writer.write_strs(std::to_string(*reinterpret_cast<const i8*>(ptr)), "i8"); break;
				case type_idx::u8: file_writer.write_strs(std::to_string(*reinterpret_cast<const u8*>(ptr)), "u8"); break;
				case type_idx::i16: file_writer.write_strs(std::to_string(*reinterpret_cast<const i16*>(ptr)), "i16"); break;
				case type_idx::u16: file_writer.write_strs(std::to_string(*reinterpret_cast<const u16*>(ptr)), "u16"); break;
				case type_idx::i32: file_writer.write_strs(std::to_string(*reinterpret_cast<const i32*>(ptr)), "i32"); break;
				case type_idx::u32: file_writer.write_strs(std::to_string(*reinterpret_cast<const u32*>(ptr)), "u32"); break;
				case type_idx::i64: file_writer.write_strs(std::to_string(*reinterpret_cast<const i64*>(ptr)), "i64"); break;
				case type_idx::u64: file_writer.write_strs(std::to_string(*reinterpret_cast<const u64*>(ptr)), "u64"); break;
				case type_idx::f32: file_writer.write_strs(std::to_string(*reinterpret_cast<const f32*>(ptr)), "f"); break;
				case type_idx::f64: file_writer.write_str(std::to_string(*reinterpret_cast<const f64*>(ptr))); break;
				case type_idx::vptr: file_writer.write_str(null_keyword); break;
				default: ASSERT(false, "Unknown constant type");
			}
		}
		void write_hex(size_t value)
		{
			file_writer.write_str("0x");
			constexpr size_t nibble_count = sizeof(size_t) * 2;
			for (size_t i = 0; i < nibble_count; i++)
			{
				const size_t nibble = (value >> ((nibble_count - 1) * 4)) & size_t(0xF);
				if (nibble < 10)
				{
					file_writer.write_str('0' + char(nibble));
				}
				else
				{
					file_writer.write_str('A' + char(nibble - 10));
				}
				value <<= 4;
			}
		}
		void write_constant(const_pointer_t& ptr, type_idx type, bool top_level = false)
		{
			if (top_level) file_writer.write_space();

			const auto& t = data.types[type];

			if (t.is_pointer())
			{
				write_hex(*reinterpret_cast<const size_t*>(ptr));
				ptr += get_base_type_size(type_idx::vptr);
			}
			else if (t.is_arithmetic())
			{
				write_literal(ptr, type);
				ptr += get_base_type_size(type);
			}
			else if (t.is_signature())
			{
				const size_t method_handle = *reinterpret_cast<const size_t*&>(ptr)++;
				if (method_handle == 0)
				{
					file_writer.write_str(null_keyword);
				}
				else
				{
					const method_idx call_idx = method_idx(method_handle ^ size_t(data.internal_hash));
					ASSERT(data.methods.is_valid_index(call_idx), "Invalid method index");

					file_writer.write_str(database[data.methods[call_idx].name]);
				}
			}
			else if (t.is_array())
			{
				for (size_t i = 0; i < t.generated.array.array_size; i++)
				{
					if (i != 0) file_writer.write_space();
					write_constant(ptr, t.generated.array.underlying_type);
				}
			}
			else
			{
				for (size_t i = 0; i < t.fields.size(); i++)
				{
					if (i != 0) file_writer.write_space();
					write_constant(ptr, t.fields[i].type);
				}
			}
		}
		void write_offset(offset_idx idx)
		{
			const auto& offset = data.offsets[idx];
			file_writer.write_str(resolve_type_name(offset.name.parent_type));
			for (size_t i = 0; i < offset.name.field_names.size(); i++)
			{
				file_writer.write_str(i == 0 ? ':' : '.');
				file_writer.write_str(database[offset.name.field_names[i]]);
			}
		}

		string_view resolve_type_name(const type& type)
		{
			string& str = type_names[size_t(type.index)];
			if (type_names[size_t(type.index)].empty())
			{
				data.generate_name(type.index, str);
			}
			return str;
		}
		string_view resolve_type_name(type_idx type)
		{
			return resolve_type_name(data.types[type]);
		}


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


		const assembly_data& data;
		const string_table<name_idx>& database;

		// Type constant
		const type& int_type;
		const type& offset_type;
		const type& size_type;
		const type& vptr_type;

		vector<string> type_names;
	};

	void translator_propane::generate(const char* out_file, const assembly& linked_assembly)
	{
		VALIDATE_ASSEMBLY(linked_assembly.is_valid());
		VALIDATE_COMPATIBILITY(linked_assembly.is_compatible());

		const assembly_data& data = linked_assembly.assembly_ref();
		VALIDATE_ENTRYPOINT(data.methods.is_valid_index(data.main));

		translator_propane_impl generator(out_file, data);
	}
}