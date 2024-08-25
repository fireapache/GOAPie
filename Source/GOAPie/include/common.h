#pragma once

#include <string>

#include "definitions.h"

#include "uuid_v4.h"

namespace gie
{
	inline Guid randGuid()
	{
		UUIDv4::UUIDGenerator<std::mt19937_64> uuidGenerator;
		return uuidGenerator.getUUID().hash();
	}

	inline Guid stringHasher( std::string_view value )
	{
		return std::hash< std::string_view >{}( value );
	}

	const StringHash UndefinedName{ stringHasher( "Undefined Name" ) };
}
