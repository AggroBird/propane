#ifndef _HEADER_PROPANE_PARSER
#define _HEADER_PROPANE_PARSER

#include "propane_intermediate.hpp"

namespace propane
{
	template<uint32_t language> class parser
	{
	public:
		parser() = delete;
	};

	// Experimental parser for parsing Propane from text
	class parser_propane
	{
	public:
		static intermediate parse(const char* file_path);
	};

	template<> class parser<language_propane> : public parser_propane {};
}

#endif