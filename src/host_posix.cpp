#include "host.hpp"

#if defined(HOST_SYSTEM_OTHER)

#include "errors.hpp"

// Posix implementation
#include <unistd.h>
#include <cstdlib>
#include <sys/mman.h>

namespace propane
{
	hostmem host::allocate(size_t len)
	{
		ASSERT(len, "Allocation length cannot be zero");

		// Get page size
		const size_t page_size = size_t(sysconf(_SC_PAGESIZE));
		ASSERT(page_size, "Page size is zero");

		// Round up to page size
		const size_t full_size = ceil_page_size(len, page_size);

		// Allocate
		void* address;
		const int result = ::posix_memalign(&address, page_size, full_size);
		return hostmem{ result == 0 ? address : nullptr, full_size };
	}
	bool host::protect(hostmem mem)
	{
		const int result = ::mprotect(mem.address, mem.size, PROT_READ);
		return result == 0;
	}
	void host::free(hostmem mem)
	{
		::free(mem.address);
	}
}

#endif