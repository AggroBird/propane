#include "internal_bind.hpp"

#include <cstring>
#include <cstdlib>

namespace propane
{
	// Internal method definitions
	namespace internal
	{
		void memset(void* ptr, uint8_t set, size_t len)
		{
			::memset(ptr, set, len);
		}
		void memcpy(void* dst, const void* src, size_t len)
		{
			::memcpy(dst, src, len);
		}
		void memmove(void* dst, const void* src, size_t len)
		{
			::memmove(dst, src, len);
		}

		int64_t time()
		{
			return ::time(nullptr);
		}
	}

	// Internal method binds
	const internal_callable_info internal_call_array[]
	{
		BIND_INTERNAL("malloc", ::malloc),
		BIND_INTERNAL("free", ::free),

		BIND_INTERNAL("memset", internal::memset),
		BIND_INTERNAL("memcpy", internal::memcpy),
		BIND_INTERNAL("memmove", internal::memmove),

		BIND_INTERNAL("time", internal::time),

		BIND_OVERLOAD("floor", ::floor, double(double)),
		BIND_OVERLOAD("ceil", ::ceil, double(double)),
		BIND_OVERLOAD("round", ::round, double(double)),
		BIND_OVERLOAD("sin", ::sin, double(double)),
		BIND_OVERLOAD("cos", ::cos, double(double)),
		BIND_OVERLOAD("tan", ::tan, double(double)),

		BIND_INTERNAL("rand", ::rand),
	};


	constexpr size_t icall_count = sizeof(internal_call_array) / sizeof(internal_callable_info);
	size_t internal_callable_info::initialized = 0;
	size_t internal_callable_info::hash = 0;

	const internal_call_info& get_internal_call(size_t idx)
	{
		if (internal_callable_info::initialized != icall_count)
		{
			fprintf(stderr, "Attempted to access internal method calls before they were initialized,\n"
				"internal method calls can only be accessed after global data initialization");
			abort();
		}
		if (idx >= icall_count)
		{
			fprintf(stderr, "Internal method call index out of range");
			abort();
		}
		return internal_call_array[idx];
	}
	size_t internal_call_count()
	{
		return icall_count;
	}

	size_t internal_call_hash()
	{
		return internal_callable_info::hash;
	}

	void call_internal(method_idx index, void* return_value_address, const void* parameter_stack_address)
	{
		if (size_t(index) >= icall_count)
		{
			fprintf(stderr, "Internal method call index out of range");
			abort();
		}
		internal_call_array[size_t(index)].call(reinterpret_cast<pointer_t>(return_value_address), reinterpret_cast<const_pointer_t>(parameter_stack_address));
	}
}