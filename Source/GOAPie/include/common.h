#pragma once

#include <string>

#include "definitions.h"

#include "uuid_v4.h"

namespace gie
{
	Guid randGuid()
	{
		UUIDv4::UUIDGenerator<std::mt19937_64> uuidGenerator;
		return uuidGenerator.getUUID().hash();
	}

	Guid stringHasher( std::string_view value )
	{
		return std::hash< std::string_view >{}( value );
	}

	const StringHash UndefinedActionName{ stringHasher( "Define Action Name" ) };
}
