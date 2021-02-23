#ifndef _HEADER_PROPANE_GENERATOR
#define _HEADER_PROPANE_GENERATOR

#include "propane_intermediate.hpp"

namespace propane
{
	enum : uint32_t
	{
		language_propane,
		language_c,
	};


	template<uint32_t language> class generator
	{
	public:
		generator() = delete;
	};

	class c_generator
	{
	public:
		static void generate(const char* out_dir, const class assembly& linked_assembly);
	};


	struct address
	{
		address(index_t index, address_type type,
			address_modifier modifier = address_modifier::none,
			address_prefix prefix = address_prefix::none) :
			header(type, prefix, modifier, index),
			payload(0) {}

		address_header header;
		union address_payload
		{
			address_payload() = default;
			address_payload(uint64_t init) : u64(init) {}

			int8_t i8;
			uint8_t u8;
			int16_t i16;
			uint16_t u16;
			int32_t i32;
			uint32_t u32;
			int64_t i64;
			uint64_t u64;
			float f32;
			double f64;
			void* vptr;
			name_idx global;
			offset_idx field;
			offset_t offset;
		} payload;
	};

	struct constant : public address
	{
	private:
		constant(type_idx type) : address(index_t(type), address_type::constant) {}

	public:
		constant(int8_t val) : constant(type_idx::i8) { payload.i8 = val; }
		constant(uint8_t val) : constant(type_idx::u8) { payload.u8 = val; }
		constant(int16_t val) : constant(type_idx::i16) { payload.i16 = val; }
		constant(uint16_t val) : constant(type_idx::u16) { payload.u16 = val; }
		constant(int32_t val) : constant(type_idx::i32) { payload.i32 = val; }
		constant(uint32_t val) : constant(type_idx::u32) { payload.u32 = val; }
		constant(int64_t val) : constant(type_idx::i64) { payload.i64 = val; }
		constant(uint64_t val) : constant(type_idx::u64) { payload.u64 = val; }
		constant(float val) : constant(type_idx::f32) { payload.f32 = val; }
		constant(double val) : constant(type_idx::f64) { payload.f64 = val; }
		constant(std::nullptr_t) : constant(type_idx::vptr) { payload.vptr = nullptr; }
		constant(name_idx val) : constant(type_idx::voidtype) { payload.global = val; }
	};

	struct prefixable_address : public address
	{
	protected:
		prefixable_address(index_t index, address_type type) :
			address(index, type) {}

	public:
		address operator*() const
		{
			address result = *this;
			result.header.set_prefix(address_prefix::indirection);
			return result;
		}
		address operator&() const
		{
			address result = *this;
			result.header.set_prefix(address_prefix::address_of);
			return result;
		}
		address operator!() const
		{
			address result = *this;
			result.header.set_prefix(address_prefix::size_of);
			return result;
		}
	};

	struct indirect_address : public prefixable_address
	{
		prefixable_address field(offset_idx field) const
		{
			prefixable_address result = *this;
			result.header.set_modifier(address_modifier::indirect_field);
			result.payload.field = field;
			return result;
		}
	};

	struct modifyable_address : public prefixable_address
	{
	protected:
		modifyable_address(index_t index, address_type type) :
			prefixable_address(index, type) {}

	public:
		prefixable_address field(offset_idx field) const
		{
			prefixable_address result = *this;
			result.header.set_modifier(address_modifier::direct_field);
			result.payload.field = field;
			return result;
		}
		const indirect_address* operator->() const
		{
			return reinterpret_cast<const indirect_address*>(this);
		}
		prefixable_address operator[](offset_t offset) const
		{
			prefixable_address result = *this;
			result.header.set_modifier(address_modifier::subscript);
			result.payload.offset = offset;
			return result;
		}
	};

	struct stack : public modifyable_address
	{
		stack(index_t index) :
			modifyable_address(index, address_type::stackvar) {}
	};

	struct param : public modifyable_address
	{
		param(index_t index) :
			modifyable_address(index, address_type::parameter) {}
	};

	struct retval : public modifyable_address
	{
		retval() :
			modifyable_address(address_header::index_max, address_type::stackvar) {}
	};

	struct global : public modifyable_address
	{
		global(name_idx name) :
			modifyable_address(index_t(name), address_type::global) {}
	};

	template<typename value_t> inline std::span<const value_t> init_span(std::initializer_list<value_t> init) noexcept
	{
		return std::span<const value_t>(init.begin(), init.size());
	}
	template<typename value_t> inline std::span<const value_t> init_span(const value_t& init) noexcept
	{
		return std::span<const value_t>(&init, 1);
	}

	class propane_generator
	{
	public:
		propane_generator();
		propane_generator(std::string_view name);
		~propane_generator();

		propane_generator(const propane_generator&) = delete;
		propane_generator& operator=(const propane_generator&) = delete;

		class type_writer final
		{
		public:
			type_writer(const type_writer&) = delete;
			type_writer& operator=(const type_writer&) = delete;

			void declare_field(type_idx type, name_idx name);
			void declare_field(type_idx type, std::string_view name);

			void finalize();

		private:
			friend class generator_impl;
			friend class propane_generator;
			friend class type_writer_impl;

			type_writer(class generator_impl&, name_idx, type_idx, bool);
			~type_writer();

			struct handle
			{
				uint8_t data[sizeof(size_t) * 32];
			} handle;

			inline class type_writer_impl& impl() noexcept
			{
				return reinterpret_cast<class type_writer_impl&>(handle);
			}
			inline const class type_writer_impl& impl() const noexcept
			{
				return reinterpret_cast<const class type_writer_impl&>(handle);
			}

			file_meta get_meta() const;
		};

		class method_writer final
		{
		public:
			method_writer(const method_writer&) = delete;
			method_writer& operator=(const method_writer&) = delete;

			void set_stack(std::span<const type_idx> types);
			inline void set_stack(std::initializer_list<type_idx> types)
			{
				set_stack(init_span(types));
			}

			label_idx declare_label(std::string_view label_name);
			void write_label(label_idx label);

			void write_noop();

			void write_set(address lhs, address rhs);
			void write_conv(address lhs, address rhs);

			void write_not(address lhs);
			void write_neg(address lhs);
			void write_mul(address lhs, address rhs);
			void write_div(address lhs, address rhs);
			void write_mod(address lhs, address rhs);
			void write_add(address lhs, address rhs);
			void write_sub(address lhs, address rhs);
			void write_lsh(address lhs, address rhs);
			void write_rsh(address lhs, address rhs);
			void write_and(address lhs, address rhs);
			void write_xor(address lhs, address rhs);
			void write_or(address lhs, address rhs);

			void write_padd(address lhs, address rhs);
			void write_psub(address lhs, address rhs);
			void write_pdif(address lhs, address rhs);

			void write_cmp(address lhs, address rhs);
			void write_ceq(address lhs, address rhs);
			void write_cne(address lhs, address rhs);
			void write_cgt(address lhs, address rhs);
			void write_cge(address lhs, address rhs);
			void write_clt(address lhs, address rhs);
			void write_cle(address lhs, address rhs);
			void write_cze(address lhs);
			void write_cnz(address lhs);

			void write_br(label_idx label);
			void write_beq(label_idx label, address lhs, address rhs);
			void write_bne(label_idx label, address lhs, address rhs);
			void write_bgt(label_idx label, address lhs, address rhs);
			void write_bge(label_idx label, address lhs, address rhs);
			void write_blt(label_idx label, address lhs, address rhs);
			void write_ble(label_idx label, address lhs, address rhs);
			void write_bze(label_idx label, address lhs);
			void write_bnz(label_idx label, address lhs);

			void write_sw(address addr, std::span<const label_idx> labels);
			inline void write_sw(address addr, std::initializer_list<label_idx> labels)
			{
				write_sw(addr, init_span(labels));
			}

			void write_call(method_idx method, std::span<const address> args = std::span<const address>());
			inline void write_call(method_idx method, std::initializer_list<address> args)
			{
				write_call(method, init_span(args));
			}
			void write_callv(address addr, std::span<const address> args = std::span<const address>());
			inline void write_callv(address addr, std::initializer_list<address> args)
			{
				write_callv(addr, init_span(args));
			}
			void write_ret();
			void write_retv(address addr);

			void write_dump(address addr);

			void finalize();

		private:
			friend class generator_impl;
			friend class propane_generator;
			friend class method_writer_impl;

			method_writer(class generator_impl&, name_idx, method_idx, signature_idx);
			~method_writer();

			struct handle_type
			{
				uint8_t data[sizeof(size_t) * 128];
			} handle;

			inline class method_writer_impl& impl() noexcept
			{
				return reinterpret_cast<class method_writer_impl&>(handle);
			}
			inline const class method_writer_impl& impl() const noexcept
			{
				return reinterpret_cast<const class method_writer_impl&>(handle);
			}

			file_meta get_meta() const;
		};

		name_idx make_identifier(std::string_view name);

		signature_idx make_signature(type_idx return_type, std::span<const type_idx> parameter_types = std::span<const type_idx>());
		inline signature_idx make_signature(type_idx return_type, std::initializer_list<type_idx> parameter_types)
		{
			return make_signature(return_type, init_span(parameter_types));
		}

		offset_idx make_offset(type_idx type, std::span<const name_idx> fields);
		inline offset_idx make_offset(type_idx type, std::initializer_list<name_idx> fields)
		{
			return make_offset(type, init_span(fields));
		}
		inline offset_idx make_offset(type_idx type, name_idx field)
		{
			return make_offset(type, init_span(field));
		}

		void define_global(name_idx name, bool is_constant, type_idx type, std::span<const constant> values = std::span<const constant>());
		inline void define_global(name_idx name, bool is_constant, type_idx type, std::initializer_list<constant> values)
		{
			define_global(name, is_constant, type, init_span(values));
		}
		inline void define_global(std::string_view name, bool is_constant, type_idx type, std::span<const constant> values = std::span<const constant>())
		{
			return define_global(make_identifier(name), is_constant, type, values);
		}
		inline void define_global(std::string_view name, bool is_constant, type_idx type, std::initializer_list<constant> values)
		{
			return define_global(make_identifier(name), is_constant, type, init_span(values));
		}

		type_idx declare_type(name_idx name);
		inline type_idx declare_type(std::string_view name)
		{
			return declare_type(make_identifier(name));
		}
		type_writer& define_type(type_idx type, bool is_union = false);
		inline type_writer& define_type(std::string_view name, bool is_union = false)
		{
			return define_type(declare_type(make_identifier(name)));
		}
		
		type_idx declare_pointer_type(type_idx base_type);
		type_idx declare_array_type(type_idx base_type, size_t array_size);
		type_idx declare_signature_type(signature_idx signature);

		method_idx declare_method(name_idx name);
		inline method_idx declare_method(std::string_view name)
		{
			return declare_method(make_identifier(name));
		}
		method_writer& define_method(method_idx method, signature_idx signature);
		inline method_writer& define_method(std::string_view name, signature_idx signature)
		{
			return define_method(declare_method(make_identifier(name)), signature);
		}

		void set_line_number(index_t line_number) noexcept;

		intermediate finalize();

		static intermediate parse(const char* file_path);
		static void generate(const char* out_dir, const class assembly& linked_assembly);

		file_meta get_meta() const;

	private:
		friend class generator_impl;

		struct handle_type
		{
			uint8_t data[sizeof(size_t) * 128];
		} handle;

		inline class generator_impl& impl() noexcept
		{
			return reinterpret_cast<class generator_impl&>(handle);
		}
		inline const class generator_impl& impl() const noexcept
		{
			return reinterpret_cast<const class generator_impl&>(handle);
		}
	};

	template<> class generator<language_propane> : public propane_generator {};
	template<> class generator<language_c> : public c_generator {};
}

#endif