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
}

#endif