#ifndef _HEADER_HOST
#define _HEADER_HOST

#include "common.hpp"

#if defined(_WIN32)
#define HOST_SYSTEM_WINDOWS 1
#else
#define HOST_SYSTEM_OTHER 1
#endif

namespace propane
{
	struct hostmem
	{
		void* address;
		size_t size;

		inline operator bool() const noexcept
		{
			return address != nullptr;
		}
	};

	namespace host
	{
		hostmem allocate(size_t);
		bool protect(hostmem);
		void free(hostmem);
	}

	class host_memory final
	{
	public:
		NOCOPY_CLASS_DEFAULT(host_memory, size_t len)
		{
			handle = host::allocate(len);
		}
		~host_memory()
		{
			host::free(handle);
		}

		inline bool protect()
		{
			return host::protect(handle);
		}

		inline void* data() const noexcept
		{
			return handle.address;
		}
		inline size_t size() const noexcept
		{
			return handle.size;
		}
		inline operator bool() const noexcept
		{
			return handle;
		}

	private:
		hostmem handle;
	};
}

#endif