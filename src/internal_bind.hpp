#ifndef _HEADER_INTERNAL_BIND
#define _HEADER_INTERNAL_BIND

#include "internal.hpp"

#define BIND_INTERNAL(name) bind_method<__LINE__>(#name, internal::name)
#define BIND_INTERNAL_FUNC(name, func) bind_method<__LINE__>(name, func)

namespace propane
{
	// Internal call templates
	template<typename... parameters> class method_signature_param;
	template<> class method_signature_param<>
	{
	public:
		static inline void generate(stackvar* result, size_t& offset) noexcept
		{

		}
		template<size_t idx, typename... tuple_args> static inline void read_value(tuple<tuple_args...>& tup, const_pointer_t& data) noexcept
		{

		}
	};
	template<typename param, typename... parameters> class method_signature_param<param, parameters...> : public method_signature_param<parameters...>
	{
	public:
		static inline void generate(stackvar* result, size_t& offset) noexcept
		{
			constexpr type_idx val = derive_type_index<param>::value;
			static_assert(val != type_idx::invalid, "Unsupported base type provided");
			*result++ = stackvar(val, offset);
			offset += derive_base_size<param>::value;
			method_signature_param<parameters...>::generate(result, offset);
		}
		template<size_t idx, typename... tuple_args> static inline void read_value(tuple<tuple_args...>& tup, const_pointer_t& data) noexcept
		{
			std::get<idx>(tup) = *reinterpret_cast<const param*&>(data)++;
			method_signature_param<parameters...>::read_value<idx + 1>(tup, data);
		}
	};

	template<typename return_type, typename... parameters> class method_invoke
	{
	public:
		using forward_call = return_type(*)(parameters...);

		static inline void invoke(pointer_t ret_val, const_pointer_t param, forward_call call) noexcept
		{
			tuple<parameters...> tup;

			method_signature_param<parameters...>::read_value<0>(tup, param);

			*reinterpret_cast<return_type*>(ret_val) = expand(call, tup);
		}
	};
	template<typename... parameters> class method_invoke<void, parameters...>
	{
	public:
		using forward_call = void(*)(parameters...);

		static inline void invoke(pointer_t ret_val, const_pointer_t param, forward_call call) noexcept
		{
			tuple<parameters...> tup;

			method_signature_param<parameters...>::read_value<0>(tup, param);

			expand(call, tup);
		}
	};


	using internal_call = void(*)(pointer_t, const_pointer_t);

	struct internal_callable_info : public internal_call_info
	{
		internal_call call = nullptr;

		static size_t initialized;
		static size_t hash;
	};

	template<size_t unique_id, typename return_type, typename... parameters> internal_callable_info bind_method(string_view name, return_type(*method)(parameters...))
	{
		using forward_call = return_type(*)(parameters...);

		static forward_call call = method;

		struct bind
		{
			static void invoke(pointer_t ret_val, const_pointer_t param) noexcept
			{
				method_invoke<return_type, parameters...>::invoke(ret_val, param, call);
			}
		};

		const bool first = internal_callable_info::initialized == 0;

		// Create callable
		internal_callable_info result;
		result.index = method_idx(internal_callable_info::initialized++);
		result.name = name;
		result.return_type = derive_type_index<return_type>::value;
		result.parameters = block<stackvar>(sizeof...(parameters));
		method_signature_param<parameters...>::generate(result.parameters.data(), result.parameters_size);
		result.call = bind::invoke;

		// Update hash
		internal_callable_info::hash = first ? fnv::hash(name) : fnv::append(internal_callable_info::hash, name);
		internal_callable_info::hash = fnv::append(internal_callable_info::hash, result.return_type);
		internal_callable_info::hash = fnv::append(internal_callable_info::hash, result.return_type);
		for (const auto& it : result.parameters) internal_callable_info::hash = fnv::append(internal_callable_info::hash, it.type);

		return result;
	}
}

#endif