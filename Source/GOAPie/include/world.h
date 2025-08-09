#pragma once

#include "blackboard.h"

namespace gie
{
	class World : public IDataEntityManager
	{
		Blackboard _context{ this };

    public:
		World() = default;
		~World() = default;

		auto& context() { return _context; };
		const auto& context() const { return _context; };
		auto& properties() { return _context.properties(); };
		const auto& properties() const { return _context.properties(); };

		// IDataEntityManager interface
		class Agent* createAgent( std::string_view name = "" )	override { return _context.createAgent( name ); };
		void removeAgent( const Guid guid )						override { _context.removeAgent( guid ); };
		Entity* createEntity( std::string_view name = "", Blackboard* context = nullptr )		override { return _context.createEntity( name, context ); };
		void removeEntity( const Guid guid )					override { _context.removeEntity( guid ); };
		void removeProperty( const Guid guid )					override { _context.removeProperty( guid ); };
		void eraseAll()											override { _context.eraseAll(); };
		Property* property( const Guid guid )					override { return  _context.property( guid ); }
		const Property* property( const Guid guid ) const		override { return  _context.property( guid ); }
		Entity* entity( const Guid guid )						override { return  _context.entity( guid ); }
		const Entity* entity( const Guid guid ) const			override { return  _context.entity( guid ); }
		class Agent* agent( const Guid guid )					override { return _context.agent( guid ); }
		const class Agent* agent( const Guid guid ) const		override { return _context.agent( guid ); }

		Property* createProperty( Guid guid, StringHash hash, Guid owner = NullGuid, Property::Variant defaultValue = false ) override
		{
			return _context.createProperty( guid, hash, owner, defaultValue );
		};
	};

	inline Entity::Entity( World* world, std::string_view name, Blackboard* context ) noexcept
		: _world( world )
		, _guid( randGuid() )
		, _nameHash( stringHasher( name ) )
		, _context( context ? context : ( world ? &world->context() : nullptr ) )
	{
	}

	inline Agent::Agent( World* world, std::string_view name )
		: Entity::Entity( world, name ), _opinions( world, &world->context() )
	{
	}

	inline Property* Entity::property( StringHash hash )
	{
		if( !_context )
		{
			return nullptr;
		}

		// getting guid of property
		auto propertyGuid = _propertyGuids.find( hash );

		// property doesn't exist in this data entity
		if( propertyGuid == _propertyGuids.end() )
		{
			return nullptr;
		}

		// getting actual property
		const auto propertyPtr = _context->property( propertyGuid->second );

		return propertyPtr;
	}

	inline const Property* Entity::property( StringHash hash ) const
	{
		if( !_context )
		{
			return nullptr;
		}

		// getting guid of property
		auto propertyGuid = _propertyGuids.find( hash );

		// property doesn't exist in this data entity
		if( propertyGuid == _propertyGuids.end() )
		{
			return nullptr;
		}

		// getting actual property
		const auto propertyPtr = _context->property( propertyGuid->second );

		return propertyPtr;
	}

	inline Property* Entity::createProperty( StringHash nameHash )
	{
		if( !_context )
		{
			return nullptr;
		}

		// checking if it's already registered
		if( auto existentProperty = property( nameHash ) )
		{
			return existentProperty;
		}
		
		// registering new property
		Guid newRandGuid{ randGuid() };
		auto emplaceResult = _context->createProperty( newRandGuid, nameHash, _guid );

		if( !emplaceResult )
		{
			return nullptr;
		}

		// mapping property hash to property guid
		auto registeredPropertyGuid = _propertyGuids.emplace( nameHash, newRandGuid );
		if( registeredPropertyGuid.second )
		{
			return emplaceResult;
		}

		// erasing property in case its guid could not be registered in data entity
		_context->properties().erase( newRandGuid );

		return nullptr;
	}
}