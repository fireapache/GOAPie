#pragma once

#include <string_view>
#include <unordered_map>

#include "property.h"
#include "common.h"

namespace gie
{
	// Forward declarations
	class World;
	class Blackboard;

	class IDataEntityManager
	{
	public:
		// @return Pointer to a new agent entity.
		virtual class Agent* createAgent( std::string_view name ) = 0;
		virtual void removeAgent( const Guid guid ) = 0;
		// @return Pointer to a new data entity.
		virtual class Entity* createEntity( std::string_view name = "", Blackboard* context = nullptr ) = 0;
		virtual void removeEntity( const Guid guid ) = 0;
		// @return Pointer to a new (orphan) property.
		// NOTE: it is not assigned to an entity!
		virtual class Property* createProperty( Guid guid, StringHash hash, Guid owner = NullGuid, Property::Variant defaultValue = false ) = 0;
		virtual void removeProperty( const Guid guid ) = 0;
		virtual void eraseAll() = 0;
		// @return Pointer to an existing property.
		virtual Property* property( const Guid guid ) = 0;
		// @return Pointer to an existing property.
		virtual const Property* property( const Guid guid ) const = 0;
		// @return Pointer to an existing data entity.
		virtual Entity* entity( const Guid guid ) = 0;
		// @return Pointer to an existing data entity.
		virtual const Entity* entity( const Guid guid ) const = 0;
		// @return Pointer to an existing agent.
		virtual class Agent* agent( const Guid guid ) = 0;
		// @return Pointer to an existing agent.
		virtual const class Agent* agent( const Guid guid ) const = 0;
	};

	class Entity
	{
		friend class EntityTagRegister;

	protected:
		class World* _world;
		Guid _guid{ NullGuid };
		std::unordered_map< StringHash, Guid > _propertyGuids;
		TagSet _tags;
		StringHash _nameHash{ InvalidStringHash };
		class Blackboard* _context = nullptr;
		
	public:
		Entity() = delete;
		Entity( class World* world, std::string_view name = "", Blackboard* context = nullptr ) noexcept;
		Entity( const Entity& ) noexcept = default;
		Entity( Entity&& ) noexcept = default;
		~Entity() = default;

		Guid guid() const { return _guid; }
		TagSet& tags() { return _tags; }
		const TagSet& tags() const { return _tags; }
		bool hasTag( Tag tag ) const { return _tags.find( tag ) != _tags.end(); }
		const auto& properties() const { return _propertyGuids; }
		auto nameHash() const { return _nameHash; }
		void setContext( Blackboard* context ) { _context = context; }

		// Register a property in data entity and world using its literal name.
		// @param name: Property contextual name (e.g. "AmmoCount")
		// @return Pointer to property.
		Property* createProperty( std::string_view name )
		{
			return createProperty( stringHasher( name ) );
		}

		template< typename T >
		std::enable_if_t< std::is_convertible_v< T, std::string_view >, Property* >
		createProperty( std::string_view name, T value )
		{
			return createProperty( stringHasher( name ), stringHasher( value ) );
		}

		// Register a property in data entity and world using its literal name.
		// @param name: Property contextual name (e.g. "AmmoCount")
		// @param value: Default value of property
		// @return Pointer to property.
		Property* createProperty( std::string_view name, Property::Variant value )
		{
			return createProperty( stringHasher( name ), value );
		}

		// Register a property in data entity and world using its name hash.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @return Pointer to property.
		Property* createProperty( StringHash nameHash );

		// Register a property in data entity and world using its name hash.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @param value: Default value of property
		// @return Pointer to property.
		Property* createProperty( StringHash nameHash, Property::Variant value )
		{
			auto ppt = createProperty( nameHash );
			if( ppt )
			{
				ppt->value = value;
			}
			return ppt;
		}

		// Fetch a property registered in this data entity.
		// @param name: Property contextual name (e.g. "AmmoCount")
		// @return Pointer to property.
		Property* property( std::string_view name )
		{
			return property( stringHasher( name ) );
		}

		// Fetch a property registered in this data entity.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @return Pointer to property.
		Property* property( StringHash nameHash );

		// Fetch a property registered in this data entity.
		// @param name: Property contextual name (e.g. "AmmoCount")
		// @return Const pointer to property.
		const Property* property( std::string_view name ) const
		{
			return property( stringHasher( name ) );
		}

		// Fetch a property registered in this data entity.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @return Const pointer to property.
		const Property* property( StringHash nameHash ) const;

		void removeProperty( StringHash hash ) { _propertyGuids.erase( hash ); }

		class World* world() const { return _world; }
	};

	inline bool isTagged( const Entity* entity, Tag tag )
	{
		if( entity )
		{
			const auto& entityTags = entity->tags();
			return entityTags.find( tag ) != entityTags.cend();
		}
		return false;
	}
}