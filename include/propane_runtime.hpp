#ifndef _HEADER_PROPANE_RUNTIME
#define _HEADER_PROPANE_RUNTIME

#include "propane_common.hpp"
#include "propane_block.hpp"

namespace propane
{
	struct field
	{
		field() = default;
		field(name_idx name, type_idx type, size_t offset = 0) :
			name(name),
			type(type),
			offset(offset) {}

		name_idx name;
		type_idx type;
		aligned_size_t offset;
	};

	struct stackvar
	{
		stackvar() = default;
		stackvar(type_idx type, size_t offset = 0) :
			type(type),
			offset(offset) {}

		type_idx type;
		aligned_size_t offset;
	};

	struct generated_type
	{
		struct pointer_data
		{
			pointer_data() = default;
			pointer_data(type_idx underlying_type, size_t underlying_size) :
				underlying_type(underlying_type),
				underlying_size(underlying_size) {}

			type_idx underlying_type;
			aligned_size_t underlying_size;
		};

		struct array_data
		{
			array_data() = default;
			array_data(type_idx underlying_type, size_t array_size) :
				underlying_type(underlying_type),
				array_size(array_size) {}

			type_idx underlying_type;
			aligned_size_t array_size;
		};

		struct signature_data
		{
			signature_data() = default;
			signature_data(signature_idx index) :
				index(index),
				zero(0) {}

			signature_idx index;

		private:
			// For initialization
			aligned_size_t zero;
		};

		generated_type() = default;
		generated_type(int32_t) :
			pointer(type_idx(0), 0) {}
		generated_type(const pointer_data& pointer) :
			pointer(pointer) {}
		generated_type(const array_data& array) :
			array(array) {}
		generated_type(const signature_data& signature) :
			signature(signature) {}

		union
		{
			pointer_data pointer;
			array_data array;
			signature_data signature;
		};
	};

	struct metadata
	{
		meta_idx index;
		index_t line_number;
	};

	enum class type_flags : index_t
	{
		none = 0,
		is_union = 1 << 0,
		is_internal = 1 << 1,

		is_pointer_type = 1 << 8,
		is_array_type = 1 << 9,
		is_signature_type = 1 << 10,

		is_generated_type = (is_pointer_type | is_array_type | is_signature_type),
	};

	constexpr type_flags operator|(type_flags lhs, type_flags rhs) noexcept
	{
		return type_flags(index_t(lhs) | index_t(rhs));
	}
	constexpr type_flags& operator|=(type_flags& lhs, type_flags rhs) noexcept
	{
		lhs = lhs | rhs;
		return lhs;
	}
	constexpr bool operator&(type_flags lhs, type_flags rhs) noexcept
	{
		return type_flags(index_t(lhs) & index_t(rhs)) != type_flags::none;
	}

	constexpr bool is_integral(type_idx type) noexcept
	{
		return type < type_idx::f32;
	}
	constexpr bool is_unsigned(type_idx type) noexcept
	{
		return is_integral(type) && (((index_t)type & 1) == 1);
	}
	constexpr bool is_floating_point(type_idx type) noexcept
	{
		return type == type_idx::f32 || type == type_idx::f64;
	}
	constexpr bool is_arithmetic(type_idx type) noexcept
	{
		return type <= type_idx::f64;
	}

	struct type
	{
		name_idx name;
		type_idx index;
		type_flags flags;
		generated_type generated;
		static_block<field> fields;
		aligned_size_t total_size;
		type_idx pointer_type;
		metadata meta;

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
	};

	struct signature
	{
		signature_idx index;
		type_idx return_type;
		static_block<stackvar> parameters;
		aligned_size_t parameters_size;

		inline bool has_return_value() const noexcept
		{
			return return_type != type_idx::voidtype;
		}
	};

	struct method
	{
		name_idx name;
		method_idx index;
		type_flags flags;
		signature_idx signature;
		static_block<uint8_t> bytecode;
		static_block<aligned_size_t> labels;
		static_block<stackvar> stackvars;
		aligned_size_t stack_size;
		metadata meta;

		inline bool is_internal() const noexcept
		{
			return flags & type_flags::is_internal;
		}
	};


	struct field_address
	{
		type_idx parent_type;
		static_block<name_idx> field_names;
	};

	struct field_offset
	{
		field_address name;
		type_idx type;
		aligned_size_t offset;
	};

	struct data_table
	{
		indexed_static_block<global_idx, field> info;
		static_block<uint8_t> data;
	};

	template<typename key_t> struct string_table
	{
		static_block<string_offset> entries;
		static_block<char> strings;

		inline std::string_view operator[](key_t key) const noexcept
		{
			if (is_valid_index(key))
			{
				const auto& entry = entries[size_t(key)];
				return std::string_view(strings.data() + entry.offset, entry.length);
			}
			return std::string_view();
		}
		inline bool is_valid_index(key_t key) const noexcept
		{
			return size_t(key) < entries.size();
		}
	};

	struct assembly_data
	{
		indexed_static_block<type_idx, type> types;
		indexed_static_block<method_idx, method> methods;
		indexed_static_block<signature_idx, signature> signatures;
		indexed_static_block<offset_idx, field_offset> offsets;

		data_table globals;
		data_table constants;

		string_table<name_idx> database;
		string_table<meta_idx> metatable;
		method_idx main;
		aligned_hash_t internal_hash;

		void generate_name(type_idx type, std::string& out_name) const;
	};

	void call_internal(method_idx index, void* return_value_address, const void* parameter_stack_address);

	struct runtime_parameters
	{
		runtime_parameters() :
			max_stack_size(1 << 20),
			min_stack_size(1 << 15),
			max_callstack_depth(1024) {}

		size_t max_stack_size;
		size_t min_stack_size;
		size_t max_callstack_depth;
	};

	int32_t execute_assembly(const class assembly& linked_assembly, runtime_parameters parameters = runtime_parameters());
}

#endif