#pragma once

#include <unordered_map>
#include <variant>
#include <vector>

#include "common.h"
#include "glm/glm.hpp"

namespace gie
{
	typedef std::variant<
		bool,
		std::vector< bool >,
		float,
		std::vector< float >,
		int32_t,
		std::vector< int32_t >,
		Guid,
		std::vector< Guid >,
		glm::vec3,
		std::vector< glm::vec3 > > ArgumentType;

	typedef std::pair< StringHash, ArgumentType > NamedArgument;

	inline Checksum getChecksum( const std::vector< Guid >& guids )
	{
		Checksum type = NullArguments;
		for( const auto& guid : guids )
		{
			type += static_cast< Checksum >( guid );
		}
		return type;
	}

	inline Checksum getChecksum( std::vector< Guid >&& guids )
	{
		return getChecksum( guids );
	}

	inline Checksum getChecksum( const std::unordered_map< StringHash, ArgumentType >& namedArguments )
	{
		Checksum type = NullArguments;
		for( const auto& namedArgument : namedArguments )
		{
			type += static_cast< Checksum >( namedArgument.first );
		}
		return type;
	}

	inline Checksum getChecksum( std::unordered_map< StringHash, ArgumentType >&& namedArguments )
	{
		return getChecksum( namedArguments );
	}

	class NamedArguments
	{
		// it's checksum of all string hashes in arguments.
		Checksum _type{ NullArguments };
		std::unordered_map< StringHash, ArgumentType > _arguments{ };

		void updateType()
		{
			_type = getChecksum( _arguments );
		}

	public:

		size_t type() const { return _type; }

		NamedArguments() = default;
		NamedArguments( const std::unordered_map< StringHash, ArgumentType >& arguments )
			: _arguments( arguments )
		{
			updateType();
		};

		NamedArguments( std::unordered_map< StringHash, ArgumentType >&& arguments )
			: _arguments( std::move( arguments ) )
		{
			updateType();
		};

		NamedArguments( const NamedArguments& other )
			: _arguments( other._arguments ),
			_type( other._type ) { };

		NamedArguments( NamedArguments&& other )
			: _arguments( std::move( other._arguments ) ),
			_type( other._type ) { };

		~NamedArguments() = default;

		bool empty() const
		{
			return _arguments.empty();
		}

		auto& storage()
		{
			return _arguments;
		}

		const auto& storage() const
		{
			return _arguments;
		}

		void add( const NamedArgument& newArgument )
		{
			auto itr = _arguments.find( newArgument.first );
			if( itr != _arguments.end() )
			{
				itr->second = newArgument.second; // Assign if found
			}
			else
			{
				_arguments.emplace( newArgument ); // Insert if not found
			}
			updateType();
		}

		void add( const std::vector< NamedArgument >& arguments )
		{
			if( arguments.empty() )
			{
				return;
			}

			for( auto& argument : arguments )
			{
				_arguments.emplace( argument );
			}

			updateType();
		}

		void add( std::string_view name, ArgumentType value )
		{
			add( NamedArgument{ stringHasher( name ), value } );
		}

		const ArgumentType* get( std::string_view name ) const
		{
			return get( stringHasher( name.data() ) );
		}

		const ArgumentType* get( const char* name ) const
		{
			return get( stringHasher( name ) );
		}

		const ArgumentType* get( const StringHash hash ) const
		{
			for( const auto& argument : _arguments )
			{
				if( argument.first == hash )
				{
					return &argument.second;
				}
			}
			return nullptr;
		}

		ArgumentType* get( std::string_view name )
		{
			return get( stringHasher( name.data() ) );
		}

		ArgumentType* get( const char* name )
		{
			return get( stringHasher( name ) );
		}

		ArgumentType* get( const StringHash hash )
		{
			for( auto& argument : _arguments )
			{
				if( argument.first == hash )
				{
					return &argument.second;
				}
			}
			return nullptr;
		}
	};
}