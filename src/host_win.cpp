#include "host.hpp"

#if defined(HOST_SYSTEM_WINDOWS)

#include "errors.hpp"

// Winapi implementation
#include <Windows.h>

namespace propane
{
	hostmem host::allocate(size_t len)
	{
		ASSERT(len, "Allocation length cannot be zero");

		// Get page size
		SYSTEM_INFO system_info = { 0 };
		GetSystemInfo(&system_info);
		const size_t page_size = size_t(system_info.dwPageSize);
		ASSERT(page_size, "Page size is zero");

		// Round up to page size
		const size_t full_size = ceil_page_size(len, page_size);

		// Allocate
		void* const address = VirtualAlloc(nullptr, full_size, MEM_COMMIT, PAGE_READWRITE);
		return hostmem{ address, full_size };
	}
	bool host::protect(hostmem mem)
	{
		DWORD old_protect;
		const BOOL result = VirtualProtect(mem.address, mem.size, PAGE_READONLY, &old_protect);
		return result;
	}
	void host::free(hostmem mem)
	{
		const BOOL result = VirtualFree(mem.address, 0, MEM_RELEASE);
		ASSERT(result, "Failed to release memory");
	}
}

#endif