#pragma once

#include <unordered_map>
#include <set>

#include "entity.h"
#include "common.h"

namespace gie
{
	// Forward declarations
	class World;
	class Blackboard;

    // Keep track of entity tags, grouping entities for tag based queries  
    class EntityTagRegister  
    {  
		friend class Simulation;  

		World* _world{ nullptr };  
		std::unordered_map< Tag, std::set< Guid > > _storage;  
		const EntityTagRegister* _parent{ nullptr }; // Parent property  

		gie::TagSet* _tagSet( Tag entityTag )  
		{  
			auto mapFind = _storage.find( entityTag );  
			if( mapFind != _storage.end() )  
			{  
				return &mapFind->second;  
			}

			gie::TagSet* set = nullptr;

			if( _parent )
			{
				set = _copyTagSet( *_parent, entityTag );
			}
			
			if( !set )
			{
				set = _ensureTagSet( entityTag );
			}

			assert( set && "Tag set not found!" );

			return set;
		}

		gie::TagSet* _ensureTagSet( Tag entityTag )  
		{  
			auto mapFind = _storage.find( entityTag );  
			if( mapFind == _storage.end() )  
			{  
				auto empl = _storage.emplace( entityTag, std::set< Guid >{ } );  
				return empl.second ? &empl.first->second : nullptr;  
			}
			return &mapFind->second;
		}  

		gie::TagSet* _copyTagSet( const EntityTagRegister& sourceRegister, Tag tag )  
		{  
			const gie::TagSet* sourceTagSet = sourceRegister.tagSet( tag );  
			gie::TagSet* targetTagSet = nullptr;
			
			if( sourceTagSet )  
			{  
				targetTagSet = _ensureTagSet( tag );
				*targetTagSet = *sourceTagSet;
			}

			return targetTagSet;
		}  

	public:  
		EntityTagRegister() = delete;  
		EntityTagRegister( World* world, const EntityTagRegister* parent = nullptr )  
			: _world( world ), _parent( parent ) { }  
		EntityTagRegister( const EntityTagRegister& other ) = default;  

		World* world() const { return _world; }  
		const EntityTagRegister* parent() const { return _parent; }  
		void setParent( const EntityTagRegister* parent ) { _parent = parent; }  

		// Clear all tag mappings for this register (does not affect parent)
		void clear() { _storage.clear(); }

		void tag( Entity* entity, std::vector< Tag >& tags )  
		{  
			if( !entity )
			{
				return;
			}

			for( Tag entityTag : tags )  
			{  
				entity->_tags.insert( entityTag );
				if( auto set = _tagSet( entityTag ) )
				{  
					set->insert( entity->guid() );
				}
			}
		}  

		void tag( Entity* entity, std::vector< Tag >&& tags )  
		{  
			tag( entity, tags );  
		}  

		void untag( Entity* entity, std::vector< Tag >& tags )  
		{  
			if( !entity )
			{
				return;
			}

			for( Tag entityTag : tags )  
			{  
				entity->_tags.erase( entityTag );  
				if( auto set = _tagSet( entityTag ) )  
				{  
					set->erase( entity->guid() );  
				}  
			} 
		}  

		void untag( Entity* entity, std::vector< Tag >&& tags )  
		{  
			untag( entity, tags );  
		}  

		const std::set< Guid >* tagSet( std::string_view tagName ) const
		{
			Tag entityTag = stringHasher( tagName );

			auto mapFind = _storage.find( entityTag );
			if( mapFind != _storage.end() )
			{
				return &mapFind->second;
			}

			// Getting from parent if possible
			if( _parent )
			{
				return _parent->tagSet( entityTag );
			}

			return nullptr;
		}

		const std::set< Guid >* tagSet( Tag entityTag ) const
		{
			auto mapFind = _storage.find( entityTag );
			if( mapFind != _storage.end() )
			{
				return &mapFind->second;
			}

			// Getting from parent if possible
			if( _parent )
			{
				return _parent->tagSet( entityTag );
			}

			return nullptr;
		}
    };

	typedef std::unordered_map< Guid, Entity > EntityMap;
	typedef std::unordered_map< Guid, Property > PropertyMap;

	class Blackboard : public IDataEntityManager
	{
		class World* _world{ nullptr };
		EntityMap _entities;
		PropertyMap _properties;
		EntityTagRegister _entityTagRegister;
		const Blackboard* _parent{ nullptr };

	public:
		Blackboard( class World* world, const Blackboard* parent = nullptr )
			: _world( world )
			, _parent( parent )
			, _entityTagRegister( world, parent ? &parent->entityTagRegister() : nullptr ) {};
		Blackboard( const Blackboard& other ) = default;
		~Blackboard() = default;

		World* world() { return _world; }
		const World* world() const { return _world; }

		EntityMap& entities() { return _entities; }
		const EntityMap& entities() const { return _entities; }

		PropertyMap& properties() { return _properties; }
		const PropertyMap& properties() const { return _properties; }

		EntityTagRegister& entityTagRegister() { return _entityTagRegister; }
		const EntityTagRegister& entityTagRegister() const { return _entityTagRegister; }

		const Blackboard* parent() const { return _parent; }
		void setParent( const Blackboard* parent ) { _parent = parent; }

		// IDataEntityManager interface
		class Agent* createAgent( std::string_view name )		override;
		Entity* createEntity( std::string_view name = "", Blackboard* context = nullptr ) override;
		Property* property( const Guid guid )				override;
		const Property* property( const Guid guid )	const	override;
		Entity* entity( const Guid guid )				override;
		const Entity* entity( const Guid guid ) const		override;
		class Agent* agent( const Guid guid )				override;
		const class Agent* agent( const Guid guid ) const	override;
		void removeAgent( const Guid guid )				override { removeEntity( guid ); }
		void removeEntity( const Guid guid )				override { _entities.erase( guid ); }
		void removeProperty( const Guid guid )				override { _properties.erase( guid ); }
		void eraseAll()								override { _entities.clear(); }

		Property* createProperty( Guid guid, StringHash hash, Guid owner = NullGuid, Property::Variant defaultValue = false )	override;

		// Return all string hashes of property names, optionally including the ones from parents
		std::set< StringHash > propertyNameHashes( bool accumulative = false ) const
		{
			std::set< StringHash > hashes;

			// Add property hashes from current Blackboard
			for( const auto& [ guid, property ] : _properties )
			{
				hashes.insert( property.hash() );
			}

			if( accumulative )
			{
				// Recursively add property hashes from the parent Blackboard, if it exists
				if( _parent )
				{
					auto parentHashes = _parent->propertyNameHashes();
					hashes.insert( parentHashes.begin(), parentHashes.end() );
				}
			}

			return hashes;
		}  
	};

	inline Entity* Blackboard::createEntity( std::string_view name, Blackboard* context )
	{
		Entity entity{ _world, name, context };
		auto result = _entities.emplace( entity.guid(), std::move( entity ) );
		if( result.second )
		{
			return &result.first->second;
		}
		return nullptr;
	}

	inline Property* Blackboard::createProperty( Guid guid, StringHash hash, Guid owner, Property::Variant defaultValue )
	{
		if( guid == NullGuid )
		{
			guid = randGuid();
		}
		Property property{ guid, owner, hash };
		auto result = _properties.emplace( guid, std::move( property ) );
		if( result.second )
		{
			result.first->second.value = defaultValue;
			return &result.first->second;
		}
		return nullptr;
	}

	inline Property* Blackboard::property( const Guid guid )
	{
		if( guid == NullGuid )
		{
			return nullptr;
		}

		auto itr = _properties.find( guid );
		if( itr != _properties.end() )
		{
			return &( itr->second );
		}

		if( parent() )
		{
			if( const Property* parentPpt = parent()->property( guid ) )
			{
				auto newPpt = createProperty( guid, parentPpt->hash(), parentPpt->ownerGuid(), parentPpt->value );
				return newPpt;
			}
		}

		return nullptr;
	}

	inline const Property* Blackboard::property( const Guid guid ) const
	{
		if( guid == NullGuid )
		{
			return nullptr;
		}

		auto itr = _properties.find( guid );
		if( itr != _properties.end() )
		{
			return &( itr->second );
		}

		if( parent() )
		{
			return parent()->property( guid );
		}

		return nullptr;
	}

	inline Entity* Blackboard::entity( const Guid guid ) 
	{
		auto itr = _entities.find( guid );
		if( itr != _entities.end() )
		{
			return &( itr->second );
		}

		// as return type is mutable, we need to copy 
		// the entity over from parent if it exists
		if( parent() )
		{
			const auto parentEntity = parent()->entity( guid );
			if( parentEntity )
			{
				auto copiedEntity = _entities.emplace( guid, *parentEntity );
				if( copiedEntity.second )
				{
					copiedEntity.first->second.setContext( this ); // Set the context to this Blackboard
				}
				return copiedEntity.second ? &copiedEntity.first->second : nullptr;
			}
		}

		return nullptr;
	}

	inline const Entity* Blackboard::entity( const Guid guid ) const
	{
		auto itr = _entities.find( guid );
		if( itr != _entities.end() )
		{
			return &( itr->second );
		}

		if( parent() )
		{
			return parent()->entity( guid );
		}

		return nullptr;
	}
}