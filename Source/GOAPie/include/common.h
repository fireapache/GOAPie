#pragma once

#include <string>

#include "definitions.h"

#include "uuid_v4.h"

namespace gie
{
	class StringRegister
	{
		std::unordered_map< StringHash, std::string > _storage;

	public:
		StringHash add( const std::string_view value )
		{
			StringHash key = static_cast< StringHash >( std::hash< std::string_view >{}( value ) );

			// checking if it is already registered
			if( _storage.find( key ) != _storage.end() )
			{
				return key;
			}

			// adding new string
			if( _storage.emplace( key, value ).second )
			{
				return key;
			}

			return InvalidStringHash;
		}

		std::string_view get( const StringHash key ) const
		{
			auto itr = _storage.find( key );
			if( itr != _storage.end() )
			{
				return itr->second;
			}
			return std::string_view{};
		}
	};

	static inline StringRegister& stringRegister()
	{
		static StringRegister instance{};
		return instance;
	}

	inline Guid randGuid()
	{
		UUIDv4::UUIDGenerator<std::mt19937_64> uuidGenerator;
		return uuidGenerator.getUUID().hash();
	}

	inline StringHash stringHasher( std::string_view value )
	{
		// string is hashed in the string register
		auto hash = stringRegister().add( value );
		return hash;
	}

	const StringHash UndefinedName{ stringHasher( "Undefined Name" ) };
}
