#ifndef _HEADER_GENERATOR
#define _HEADER_GENERATOR

#include "internal.hpp"
#include "database.hpp"

namespace propane
{
	enum class extended_flags : index_t
	{
		is_defined = 1 << 24,
		is_resolving = 1 << 25,
		is_resolved = 1 << 26,
	};
	constexpr type_flags operator|(type_flags lhs, extended_flags rhs) noexcept
	{
		return type_flags(index_t(lhs) | index_t(rhs));
	}
	constexpr type_flags operator|(extended_flags lhs, type_flags rhs) noexcept
	{
		return type_flags(index_t(lhs) | index_t(rhs));
	}
	constexpr type_flags operator|(extended_flags lhs, extended_flags rhs) noexcept
	{
		return type_flags(index_t(lhs) | index_t(rhs));
	}
	constexpr type_flags& operator|=(type_flags& lhs, extended_flags rhs) noexcept
	{
		lhs = lhs | type_flags(rhs);
		return lhs;
	}
	constexpr bool operator&(type_flags lhs, extended_flags rhs) noexcept
	{
		return type_flags(index_t(lhs) & index_t(rhs)) != type_flags::none;
	}

	class gen_type
	{
	public:
		gen_type() = default;
		gen_type(name_idx name, type_idx index) :
			name(name),
			index(index) {}
		gen_type(name_idx name, const base_type_info& btype_info) :
			gen_type(name, btype_info.type)
		{
			if (btype_info.type == type_idx::voidtype)
			{
				pointer_type = type_idx::vptr;
			}
			else if (btype_info.type == type_idx::vptr)
			{
				make_pointer(type_idx::voidtype);
			}

			flags |= (extended_flags::is_defined | extended_flags::is_resolved);
			total_size = btype_info.size;
		}

		// Type
		name_idx name = name_idx::invalid;
		type_idx index = type_idx::invalid;
		type_flags flags = type_flags::none;

		generated_type generated = 0;

		inline void make_pointer(type_idx underlying_type, size_t underlying_size = 0)
		{
			generated = generated_type::pointer_data(underlying_type, 0);
			flags |= type_flags::is_pointer_type;
		}
		inline void make_array(type_idx underlying_type, size_t array_size)
		{
			generated = generated_type::array_data(underlying_type, array_size);
			flags |= type_flags::is_array_type;
		}
		inline void make_signature(signature_idx signature)
		{
			generated = generated_type::signature_data(signature);
			flags |= type_flags::is_signature_type;
		}

		vector<field> fields;

		size_t total_size = 0;

		inline bool is_integral() const noexcept
		{
			return propane::is_integral(index);
		}
		inline bool is_floating_point() const noexcept
		{
			return propane::is_floating_point(index);
		}
		inline bool is_arithmetic() const noexcept
		{
			return propane::is_arithmetic(index);
		}

		inline bool is_pointer() const noexcept
		{
			return flags & type_flags::is_pointer_type;
		}
		inline bool is_array() const noexcept
		{
			return flags & type_flags::is_array_type;
		}
		inline bool is_signature() const noexcept
		{
			return flags & type_flags::is_signature_type;
		}
		inline bool is_generated() const noexcept
		{
			return flags & type_flags::is_generated_type;
		}

		inline bool is_struct() const noexcept
		{
			return !is_arithmetic() && !is_generated();
		}
		inline bool is_union() const noexcept
		{
			return flags & type_flags::is_union;
		}

		inline bool is_defined() const noexcept
		{
			return flags & extended_flags::is_defined;
		}
		inline bool is_resolved() const noexcept
		{
			return flags & extended_flags::is_resolved;
		}

		// Intermediate
		type_idx pointer_type = type_idx::invalid;
		map<size_t, type_idx> array_types;

		// Meta
		metadata meta = { meta_idx::invalid, 0 };
	};

	// Key generation
	template<typename value_t> inline void append_key(vector<uint8_t>& out_key, value_t val)
	{
		static_assert(std::is_unsigned_v<value_t> || std::is_unsigned_v<std::underlying_type_t<value_t>>, "Type must be unsigned");
		const uint64_t max_val = static_cast<uint64_t>(val);
		if (max_val <= (std::numeric_limits<uint8_t>::max() >> 2))
		{
			const uint8_t u8 = static_cast<uint8_t>(max_val << 2);
			append_bytecode(out_key, u8);
		}
		else if (max_val <= (std::numeric_limits<uint16_t>::max() >> 2))
		{
			const uint16_t u16 = static_cast<uint16_t>(max_val << 2) | uint16_t(1);
			append_bytecode(out_key, u16);
		}
		else if (max_val <= (std::numeric_limits<uint32_t>::max() >> 2))
		{
			const uint32_t u32 = static_cast<uint32_t>(max_val << 2) | uint32_t(2);
			append_bytecode(out_key, u32);
		}
		else
		{
			append_bytecode(out_key, uint8_t(3));
			append_bytecode(out_key, max_val);
		}
	}
	template<typename value_t> inline void make_key(type_idx type, span<const value_t> param, vector<uint8_t>& out_key)
	{
		out_key.clear();
		append_key(out_key, type);
		for (size_t i = 0; i < param.size(); i++)
		{
			append_key(out_key, param[i]);
		}
	}
	template<> inline void make_key<stackvar>(type_idx type, span<const stackvar> param, vector<uint8_t>& out_key)
	{
		out_key.clear();
		append_key(out_key, type);
		for (size_t i = 0; i < param.size(); i++)
		{
			append_key(out_key, param[i].type);
		}
	}

	// Methods
	class gen_signature
	{
	public:
		gen_signature() = default;
		gen_signature(signature_idx index, type_idx return_type, vector<stackvar>&& parameters) :
			index(index),
			return_type(return_type),
			parameters(std::move(parameters)) {}
		gen_signature(signature_idx index, type_idx return_type, const block<stackvar>& parameters) :
			index(index),
			return_type(return_type)
		{
			this->parameters.insert(this->parameters.end(), parameters.begin(), parameters.end());
		}

		// Signature
		signature_idx index = signature_idx::invalid;
		type_idx return_type = type_idx::voidtype;
		vector<stackvar> parameters;

		size_t parameters_size = 0;

		inline bool has_return_value() const noexcept
		{
			return return_type != type_idx::voidtype;
		}

		inline void make_key(vector<uint8_t>& out_key) const
		{
			return propane::make_key<stackvar>(return_type, parameters, out_key);
		}

		// Intermediate
		type_idx signature_type = type_idx::invalid;

		bool is_resolved = false;
	};

	struct gen_field_address
	{
		gen_field_address() = default;
		gen_field_address(type_idx object_type, block<name_idx>&& field_names) :
			object_type(object_type),
			field_names(std::move(field_names)) {}

		type_idx object_type = type_idx::invalid;
		block<name_idx> field_names;

		inline void make_key(vector<uint8_t>& out_key) const
		{
			return propane::make_key<name_idx>(object_type, field_names, out_key);
		}
	};

	struct gen_field_offset
	{
		gen_field_offset() = default;
		gen_field_offset(const gen_field_address& name) :
			name(name) {}

		gen_field_address name;
		type_idx type = type_idx::invalid;
		size_t offset = 0;
	};

	class gen_method
	{
	public:
		gen_method() = default;

		gen_method(name_idx name, method_idx index) :
			name(name),
			index(index) {}

		// Method
		name_idx name = name_idx::invalid;
		method_idx index = method_idx::invalid;
		type_flags flags = type_flags::none;

		signature_idx signature = signature_idx::invalid;

		vector<uint8_t> bytecode;
		vector<size_t> labels;
		vector<stackvar> stackvars;
		size_t stack_size = 0;

		inline bool is_defined() const noexcept
		{
			return flags & extended_flags::is_defined;
		}
		inline bool is_resolved() const noexcept
		{
			return flags & extended_flags::is_resolved;
		}

		inline bool is_internal() const noexcept
		{
			return flags & type_flags::is_internal;
		}

		inline void translate_method(method_idx src, method_idx dst)
		{
			for (auto& c : calls) if (c == src) c = dst;
		}

		// Intermediate
		vector<method_idx> calls;
		vector<translate_idx> globals;
		vector<offset_idx> offsets;

		// Meta
		metadata meta = { meta_idx::invalid, 0 };
	};

	// Data
	class gen_data_table
	{
	public:
		indexed_vector<global_idx, field> info;
		vector<uint8_t> data;
	};

	class gen_database : public database<name_idx, lookup_idx> {};
	class gen_metatable : public database<meta_idx, void> {};


	struct key_hash
	{
		inline size_t operator()(const vector<uint8_t>& key) const
		{
			return fnv::hash(key.data(), key.size() * sizeof(vector<uint8_t>::value_type));
		}
	};

	struct key_compare
	{
		inline bool operator()(const vector<uint8_t>& lhs, const vector<uint8_t>& rhs) const
		{
			if (lhs.size() != rhs.size()) return false;
			return std::memcmp(lhs.data(), rhs.data(), lhs.size() * sizeof(vector<uint8_t>::value_type)) == 0;
		}
	};

	// Intermediate
	class gen_intermediate_data
	{
	public:
		MOVABLE_CLASS_DEFAULT(gen_intermediate_data) = default;

		static void serialize(intermediate& dst, const gen_intermediate_data& data);
		static gen_intermediate_data deserialize(const intermediate& im);

		static gen_intermediate_data merge(gen_intermediate_data&& lhs_data, gen_intermediate_data&& rhs_data);
		static gen_intermediate_data merge(const intermediate& lhs, const intermediate& rhs);

		void initialize_base_types();
		void restore_lookup_tables();
		void restore_generated_types();

		indexed_vector<type_idx, gen_type> types;
		indexed_vector<method_idx, gen_method> methods;
		indexed_vector<signature_idx, gen_signature> signatures;
		unordered_map<vector<uint8_t>, signature_idx, key_hash, key_compare> signature_lookup;

		indexed_vector<offset_idx, gen_field_offset> offsets;
		unordered_map<vector<uint8_t>, offset_idx, key_hash, key_compare> offset_lookup;

		gen_data_table globals;
		gen_data_table constants;

		gen_database database;
		gen_metatable metatable;

		inline file_meta make_meta(type_idx type) const noexcept
		{
			const auto& meta = types[type].meta;
			return file_meta(metatable[meta.index].name, meta.line_number);
		}
		inline file_meta make_meta(method_idx method) const noexcept
		{
			const auto& meta = methods[method].meta;
			return file_meta(metatable[meta.index].name, meta.line_number);
		}
	};

	class string_writer final : public string
	{
	public:
		template<size_t len> inline void write_str(const char(&str)[len])
		{
			append(str, len - 1);
		}
		inline void write_str(string_view str)
		{
			append(str.data(), str.size());
		}
		inline void write_strs()
		{

		}
		template<typename value_t, typename... args_t> inline void write_strs(const value_t& val, const args_t&... arg)
		{
			write_str(val);
			write_strs(arg...);
		}
		inline void write_str(const char c)
		{
			push_back(c);
		}
		inline void write_space()
		{
			write_str(' ');
		}
		inline void write_tab()
		{
			write_str('\t');
		}
		inline void write_newline()
		{
			write_str('\n');
		}
	};

	namespace operations
	{
		void ari(string_writer& dst, uint32_t op, string_view lhs_addr, type_idx lhs_type, string_view rhs_addr, type_idx rhs_type);
		void cmp(string_writer& dst, uint32_t op, string_view lhs_addr, type_idx lhs_type, string_view rhs_addr, type_idx rhs_type);
	}
}

#endif