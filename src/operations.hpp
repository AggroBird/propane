#ifndef _HEADER_OPERATIONS
#define _HEADER_OPERATIONS

#include "runtime.hpp"

namespace propane
{
	namespace operations
	{
		subcode set(type_idx lhs, type_idx rhs);
		subcode conv(type_idx lhs, type_idx rhs);
		subcode ari(opcode op, type_idx lhs, type_idx rhs);
		subcode cmp(opcode op, type_idx lhs, type_idx rhs);
		subcode ptr(opcode op, type_idx lhs, type_idx rhs);

		void conv(pointer_t lhs_addr, type_idx lhs_type, const_pointer_t rhs_addr, type_idx rhs_type);
	}
}

#endif