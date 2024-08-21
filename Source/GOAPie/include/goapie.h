#pragma once

#include <set>
#include <string>
#include <unordered_map>
#include <variant>

#include "common.h"
#include "glm/glm.hpp"

namespace gie
{
	class Property
	{
		Guid _guid{ NullGuid };
		Guid _owner{ NullGuid };
		StringHash _name{ InvalidStringHash };

	public:
		Property( Guid guid, Guid owner, StringHash hash ) : _guid( guid ), _owner( owner ), _name( hash ) { }
		Property( Guid owner, StringHash hash ) : _guid( randGuid() ), _owner( owner ), _name( hash ) { }
		Property() = delete;
		Property( const Property& ) noexcept = default;
		Property( Property&& ) noexcept = default;
		~Property() = default;

		typedef std::vector< Guid > ArrayType;

		typedef std::variant<
			bool,
			float,
			int32_t,
			Guid,
			ArrayType,
			glm::vec3,
			void* > Variant;

		Variant value;

		std::pair< bool, bool >			getBool()		const { return std::pair{ type() == Boolean, std::get< bool >( value ) }; }
		std::pair< bool, float >		getFloat()		const { return std::pair{ type() == Float, std::get< float >( value ) }; }
		std::pair< bool, int32_t >		getInteger()	const { return std::pair{ type() == Integer, std::get< int32_t >( value ) }; }
		std::pair< bool, Guid >			getGuid()		const { return std::pair{ type() == GUID, std::get< Guid >( value ) }; }
		std::pair< bool, ArrayType >	getArray()		const { return std::pair{ type() == Array, std::get< ArrayType >( value ) }; }
		std::pair< bool, glm::vec3 >	getVec3()		const { return std::pair{ type() == Vec3, std::get< glm::vec3 >( value ) }; }
		std::pair< bool, void* >		getCustom()		const { return std::pair{ type() == Custom, std::get< void* >( value ) }; }

		enum Type : uint8_t
		{
			Unknow,
			Boolean,
			Float,
			Integer,
			GUID,
			Array,
			Vec3,
			Custom
		};

		// @return Type of data being stored in this property.
		Type type() const
		{
			if( std::holds_alternative< bool >			( value ) )	return Boolean;
			if( std::holds_alternative< float >			( value ) )	return Float;
			if( std::holds_alternative< int32_t >		( value ) )	return Integer;
			if( std::holds_alternative< StringHash >	( value ) ) return GUID;
			if( std::holds_alternative< ArrayType >		( value ) )	return Array;
			if( std::holds_alternative< glm::vec3 >		( value ) )	return Vec3;
			if( std::holds_alternative< void* >			( value ) )	return Custom;
			return Unknow;
		};

		// @return Guid for this property.
		Guid guid() const { return _guid; };

		// @return Guid for data entity which this property belongs to.
		Guid ownerGuid() const { return _owner; }

		// @return StringHash representing the name given to this property.
		StringHash hash() const { return _name; }
	};

	typedef std::pair< Guid, Property::Variant > Definition;
	typedef std::pair< StringHash, Property::Variant > NamedDefinition;

	class Entity
	{
		friend class EntityTagRegister;

	protected:
		class World* _world;
		Guid _guid{ NullGuid };
		std::unordered_map< StringHash, Guid > _propertyGuids;
		TagSet _tags;
		
	public:
		Entity() = delete;
		Entity( class World* world ) noexcept : _world( world ), _guid( randGuid() ) { }
		Entity( const Entity& ) noexcept = default;
		Entity( Entity&& ) noexcept = default;
		~Entity() = default;

		Guid guid() const { return _guid; }
		TagSet& tags() { return _tags; }
		const TagSet& tags() const { return _tags; }

		typedef std::pair< Guid, Property* > FetchPropertyResult;

		// Register a property in data entity and world using its literal name.
		// @param name: Property contextual name (e.g. "AmmoCount")
		// @return Guid and pointer to property.
		FetchPropertyResult createProperty( std::string_view name )
		{
			return createProperty( stringHasher( name ) );
		}

		// Register a property in data entity and world using its literal name.
		// @param name: Property contextual name (e.g. "AmmoCount")
		// @param value: Default value of property
		// @return Guid and pointer to property.
		FetchPropertyResult createProperty( std::string_view name, Property::Variant value )
		{
			return createProperty( stringHasher( name ), value );
		}

		// Register a property in data entity and world using its name hash.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @return Guid and pointer to property.
		FetchPropertyResult createProperty( StringHash nameHash );

		// Register a property in data entity and world using its name hash.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @param value: Default value of property
		// @return Guid and pointer to property.
		FetchPropertyResult createProperty( StringHash nameHash, Property::Variant value )
		{
			auto result = createProperty( nameHash );
			if( result.second )
			{
				result.second->value = value;
			}
			return result;
		}

		// Fetch a property registered in this data entity.
		// @param name: Property contextual name (e.g. "AmmoCount")
		// @return Guid and pointer to property.
		FetchPropertyResult property( std::string_view name ) const
		{
			return property( stringHasher( name ) );
		}

		// Fetch a property registered in this data entity.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @return Guid and pointer to property.
		FetchPropertyResult property( StringHash nameHash ) const;

		void removeProperty( StringHash hash ) { _propertyGuids.erase( hash ); }

		class World* world() const { return _world; }

	};

	class IDataEntityManager
	{
	public:
		// @return Guid to a new agent entity.
		virtual std::pair< Guid, class Agent* > createAgent() = 0;
		virtual void removeAgent( const Guid guid ) = 0;
		// @return Guid to a new data entity.
		virtual std::pair< Guid, class Entity* > createEntity() = 0;
		virtual void removeEntity( const Guid guid ) = 0;
		// @return Guid to a new (orphan) property.
		// NOTE: it is not assigned to an entity!
		virtual std::pair< Guid, class Property* > createProperty( Guid guid, StringHash hash, Guid owner = NullGuid, Property::Variant defaultValue = false ) = 0;
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

	// Keep track of entity tags, grouping entities for tag based queries
	class EntityTagRegister
	{
		friend class Simulation;

		World* _world{ nullptr };
		std::unordered_map< Tag, std::set< Guid > > _storage;

		std::set< Guid >* _tagSet( Tag entityTag )
		{
			auto mapFind = _storage.find( entityTag );
			if( mapFind != _storage.end() )
			{
				return &mapFind->second;
			}
			return nullptr;
		}

		std::set< Guid >* _makeEmptyTagSet( Tag entityTag )
		{
			auto mapFind = _storage.find( entityTag );
			if( mapFind != _storage.end() )
			{
				auto empl = _storage.emplace( entityTag, std::set< Guid >{ } );
				return empl.second ? &empl.first->second : nullptr;
			}
		}

		void _copyTagSet( const EntityTagRegister& sourceRegister, EntityTagRegister& targetRegister, Tag tag )
		{
			auto sourceTagSet = sourceRegister.tagSet( tag );
			auto targetTagSet = targetRegister._tagSet( tag );
			if( sourceTagSet && targetTagSet )
			{
				*targetTagSet = *sourceTagSet;
			}
		}

	public:
		EntityTagRegister() = delete;
		EntityTagRegister( World* world ) : _world( world ) { }
		EntityTagRegister( const EntityTagRegister& other ) = default;

		World* world() const { return _world; }

		void tag( Guid entityGuid, std::vector< Tag >& tags );

		void tag( Guid entityGuid, std::vector< Tag >&& tags )
		{
			tag( entityGuid, tags );
		}

		void tag( Entity* entity, std::vector< Tag >& tags )
		{
			if( entity )
			{
				for( Tag entityTag : tags )
				{
					entity->_tags.insert( entityTag );
					auto set = _tagSet( entityTag );
					if( !set )
					{
						auto empl = _storage.emplace( entityTag, std::set< Guid >{ } );
						if( empl.second )
						{
							set = &empl.first->second;
						}
					}
					if( set )
					{
						set->insert( entity->guid() );
					}
				}
			}
		}

		void tag( Entity* entity, std::vector< Tag >&& tags )
		{
			tag( entity, tags );
		}

		void untag( Guid entityGuid, std::vector< Tag >& tags );

		void untag( Guid entityGuid, std::vector< Tag >&& tags )
		{
			untag( entityGuid, tags );
		}

		void untag( Entity* entity, std::vector< Tag >& tags )
		{
			if( entity )
			{
				for( Tag entityTag : tags )
				{
					entity->_tags.erase( entityTag );
					if( auto set = _tagSet( entityTag ) )
					{
						set->erase( entity->guid() );
					}
				}
			}
		}

		void untag( Entity* entity, std::vector< Tag >&& tags )
		{
			untag( entity, tags );
		}

		const std::set< Guid >* tagSet( Tag entityTag ) const
		{
			auto mapFind = _storage.find( entityTag );
			if( mapFind != _storage.end() )
			{
				return &mapFind->second;
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
		Blackboard( class World* world, const Blackboard* parent = nullptr ) : _world( world ), _parent( parent ), _entityTagRegister( world ) {  };
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
		std::pair< Guid, class Agent* > createAgent()		override;
		std::pair< Guid, Entity* > createEntity()			override;
		Property* property( const Guid guid )				override;
		const Property* property( const Guid guid )	const	override;
		Entity* entity( const Guid guid )					override;
		const Entity* entity( const Guid guid ) const		override;
		class Agent* agent( const Guid guid )				override;
		const class Agent* agent( const Guid guid ) const	override;
		void removeAgent( const Guid guid )					override { removeEntity( guid ); }
		void removeEntity( const Guid guid )				override { _entities.erase( guid ); }
		void removeProperty( const Guid guid )				override { _properties.erase( guid ); }
		void eraseAll()										override { _entities.clear(); }

		std::pair< Guid, Property* > createProperty( Guid guid, StringHash hash, Guid owner = NullGuid, Property::Variant defaultValue = false )	override;
	};

	std::pair< Guid, Entity* > Blackboard::createEntity()
	{
		Entity entity{ _world };
		auto result = _entities.emplace( entity.guid(), std::move( entity ) );
		if( result.second )
		{
			return std::pair{ result.first->first, &result.first->second };
		}
		return std::pair{ NullGuid, nullptr };
	}

	std::pair< Guid, Property* > Blackboard::createProperty( Guid guid, StringHash hash, Guid owner, Property::Variant defaultValue )
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
			return std::pair{ result.first->first, &result.first->second };
		}
		return std::pair{ NullGuid, nullptr };
	}

	Property* Blackboard::property( const Guid guid )
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
				auto [ newPptGuid, newPpt ] = createProperty( guid, parentPpt->hash(), parentPpt->ownerGuid(), parentPpt->value );
				return newPpt;
			}
		}

		return nullptr;
	}

	const Property* Blackboard::property( const Guid guid ) const
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

	Entity* Blackboard::entity( const Guid guid ) 
	{
		auto itr = _entities.find( guid );
		if( itr != _entities.end() )
		{
			return &( itr->second );
		}
		return nullptr;
	}

	const Entity* Blackboard::entity( const Guid guid ) const
	{
		auto itr = _entities.find( guid );
		if( itr != _entities.end() )
		{
			return &( itr->second );
		}
		return nullptr;
	}

	class StringRegister
	{
		std::unordered_map< StringHash, std::string > _storage;

	public:

		StringHash add( const std::string_view value )
		{
			StringHash key = stringHasher( value );
			if( _storage.emplace( key, value ).second )
			{
				return key;
			}
			return InvalidStringHash;
		}

		std::string_view get( const StringHash key ) const
		{
			auto find = _storage.find( key );
			if( find != _storage.end() )
			{
				return find->second;
			}
			return std::string_view{};
		}
	};

	class World : public IDataEntityManager
	{
		Blackboard _context{ this };
		// TODO: string register need to be singleton and used by all worlds
		StringRegister _stringRegister{};

    public:
		World() = default;
		~World() = default;

		auto& context() { return _context; };
		const auto& context() const { return _context; };
		auto& properties() { return _context.properties(); };
		auto& stringRegiter() { return _stringRegister; }
		const auto& stringRegister() { return _stringRegister; }

		// IDataEntityManager interface
		std::pair< Guid, class Agent* > createAgent()		override { return _context.createAgent(); };
		void removeAgent( const Guid guid )					override { _context.removeAgent( guid ); };
		std::pair< Guid, Entity* > createEntity()			override { return _context.createEntity(); };
		void removeEntity( const Guid guid )				override { _context.removeEntity( guid ); };
		void removeProperty( const Guid guid )				override { _context.removeProperty( guid ); };
		void eraseAll()										override { _context.eraseAll(); };
		Property* property( const Guid guid )				override { return  _context.property( guid ); }
		const Property* property( const Guid guid ) const	override { return  _context.property( guid ); }
		Entity* entity( const Guid guid )					override { return  _context.entity( guid ); }
		const Entity* entity( const Guid guid ) const		override { return  _context.entity( guid ); }
		class Agent* agent( const Guid guid )				override { return _context.agent( guid ); }
		const class Agent* agent( const Guid guid ) const	override { return _context.agent( guid ); }

		std::pair< Guid, Property* > createProperty( Guid guid, StringHash hash, Guid owner = NullGuid, Property::Variant defaultValue = false ) override
		{
			return _context.createProperty( guid, hash, owner, defaultValue );
		};

	};

	void EntityTagRegister::tag( Guid entityGuid, std::vector< Tag >& tags )
	{
		if( world() && !tags.empty() )
		{
			auto entity = world()->entity( entityGuid );
			tag( entity, tags );
		}
	}

	void EntityTagRegister::untag( Guid entityGuid, std::vector< Tag >& tags )
	{
		if( world() && !tags.empty() )
		{
			auto entity = world()->entity( entityGuid );
			untag( entity, tags );
		}
	}

	Entity::FetchPropertyResult Entity::property( StringHash hash ) const
	{
		if( !_world )
		{
			return { NullGuid, nullptr };
		}

		// getting guid of property
		auto propertyGuid = _propertyGuids.find( hash );

		// property doesn't exist in this data entity
		if( propertyGuid == _propertyGuids.end() )
		{
			return { NullGuid, nullptr };
		}

		// getting actual property
		const auto propertyItr = _world->properties().find( propertyGuid->second );
		if( propertyItr == _world->properties().end() )
		{
			return { NullGuid, nullptr };
		}

		return { propertyItr->first, &( propertyItr->second ) };
	}

	Entity::FetchPropertyResult Entity::createProperty( StringHash nameHash )
	{
		if( !_world )
		{
			return { NullGuid, nullptr };
		}

		// checking if it's already registered
		auto existentProperty = property( nameHash );
		if( existentProperty.second )
		{
			return existentProperty;
		}
		
		// registering new property
		Guid newRandGuid{ randGuid() };
		auto emplaceResult = _world->properties().emplace( newRandGuid, std::move( Property{ newRandGuid, _guid, nameHash } ) );

		if( !emplaceResult.second )
		{
			return { NullGuid, nullptr };
		}

		// mapping property hash to property guid
		auto registeredPropertyGuid = _propertyGuids.emplace( nameHash, emplaceResult.first->first );
		if( registeredPropertyGuid.second )
		{
			return { emplaceResult.first->first, &( emplaceResult.first->second ) };
		}

		// erasing property in case its guid could not be registered in data entity
		_world->properties().erase( emplaceResult.first->first );

		return { NullGuid, nullptr };
	}

	// Represents a NPC in world context.
	class Agent : public Entity
	{
		Blackboard _opinions;

	public:
		Agent() = delete;
		Agent( World* world ) : Entity::Entity( world ), _opinions( world, &world->context() ) { };
		Agent( const Agent& agent ) = default;
		Agent( Agent&& agent ) = default;
		~Agent() = default;

		Blackboard& opinions() { return _opinions; }
		const Blackboard& opinions() const { return _opinions; }
	};

	// Represents a NPC in simulation context. World context agent is referenced for opinions.
	class SimAgent
	{
		const Agent* _agent{ nullptr };
		Blackboard _opinions;

	public:
		SimAgent() = delete;
		SimAgent( const Agent* agent ) : _agent( agent ), _opinions( agent->world(), &agent->opinions() ) { };

		Blackboard& opinions() { return _opinions; }
		const Blackboard& opinions() const { return _opinions; }

		const Agent* worldContextAgent() const { return _agent; }

	};

	Agent* Blackboard::agent( const Guid guid )
	{
		Entity* dataEntity = entity( guid );
		return static_cast< Agent* >( dataEntity );
	}

	const Agent* Blackboard::agent( const Guid guid ) const
	{
		const Entity* dataEntity = entity( guid );
		return static_cast< const Agent* >( dataEntity );
	}

	std::pair< Guid, Agent* > Blackboard::createAgent()
	{
		Agent agent{ _world };
		auto result = _entities.emplace( agent.guid(), std::move( agent ) );
		if( result.second )
		{
			return std::pair{ result.first->first, static_cast< Agent* >( &result.first->second ) };
		}
		return std::pair{ NullGuid, nullptr };
	}

	bool isTagged( const Entity* entity, Tag tag )
	{
		if( entity )
		{
			const auto& entityTags = entity->tags();
			return entityTags.find( tag ) != entityTags.cend();
		}
		return false;
	}

	class Heuristic
	{
    public:
		Heuristic() = default;
		~Heuristic() = default;

		enum Type : uint8_t
		{
			SmallerBetter,
			GreaterBetter
		};

		virtual Type type() { return SmallerBetter; }

		struct Result
		{
			Heuristic::Type type{ Heuristic::Type::SmallerBetter };
			float value{ InvalidHeuristic };
		};

		virtual Result calculate( const World& world, const Agent& agent ) { return Result{ }; };

	};

	// Class representing a simulation of a performable action.
	// It stores all information necessary for planner's A* algorithm.
	// It also stores a list of actions to be performed by the agent,
	// in case the simulation is chosed as a valid goal path.
	class Simulation
	{
		Guid _guid{ NullGuid };
		Blackboard _context;
		World* _world{ nullptr };
		SimAgent _simAgent;

		void _syncWorldTagSet( Tag entityTag )
		{
			auto& simulationTagRegister = context().entityTagRegister();
			auto simulationTagSet = simulationTagRegister._tagSet( entityTag );
			auto& worldTagRegister = world()->context().entityTagRegister();

			if( auto worldTagSet = worldTagRegister.tagSet( entityTag ) )
			{
				// world context has tag set, copying it over to simulation context
				simulationTagSet = simulationTagRegister._makeEmptyTagSet( entityTag );
				simulationTagRegister._copyTagSet( worldTagRegister, simulationTagRegister, entityTag );
			}
			else
			{
				// create tag set in simulation context if none exist in world context
				simulationTagSet = simulationTagRegister._makeEmptyTagSet( entityTag );
			}
		}

	public:
		Simulation() = delete;
		Simulation( World* world, const SimAgent& simAgent )
			: _world( world ),
			_guid( randGuid() ),
			_context( world ),
			_simAgent( simAgent ) { }

		Simulation( Guid guid, World* world, const SimAgent& simAgent )
			: _world( world ),
			_guid( guid ),
			_context( world, &world->context() ),
			_simAgent( simAgent ) { }

		Simulation( Guid guid, World* world, const Blackboard& parentContext, const SimAgent& simAgent )
			: _world( world ),
			_guid( guid ),
			_context( parentContext ),
			_simAgent( simAgent ) { }

		Simulation( Simulation&& ) = default;
		~Simulation() = default;

		Guid guid() const { return _guid; }
		const Blackboard& context() const { return _context; }
		Blackboard& context() { return _context; }
		World* world() { return _world; }
		const World* world() const { return _world; }
		SimAgent& agent() { return _simAgent; }
		const SimAgent& agent() const { return _simAgent; }

		float cost{ MaxCost };
		float heuristic{ MaxHeuristic };
		size_t depth{ 0 };

		// Tag entity in simulation context.
		void tag( Entity* entity, std::vector< Tag >& tags )
		{
			if( !entity )
			{
				return;
			}

			for( auto entityTag : tags )
			{
				auto& simulationTagRegister = context().entityTagRegister();
				auto simulationTagSet = simulationTagRegister._tagSet( entityTag );

				// no tag set in simulation context yet, need to check world context
				if( !simulationTagSet )
				{
					_syncWorldTagSet( entityTag );
				}

				// tagging entity in simulation context
				simulationTagRegister.tag( entity, { entityTag } );
			}
		}

		// Untag entity in simulation context.
		void untag( Entity* entity, std::vector< Tag >& tags )
		{
			if( !entity )
			{
				return;
			}

			for( auto entityTag : tags )
			{
				auto& simulationTagRegister = context().entityTagRegister();
				auto simulationTagSet = simulationTagRegister._tagSet( entityTag );

				// no tag set in simulation context yet, need to check world context
				if( !simulationTagSet )
				{
					_syncWorldTagSet( entityTag );
				}

				// tagging entity in simulation context
				simulationTagRegister.untag( entity, { entityTag } );
			}
		}

		// Return set of entities with tag
		const std::set< Guid >* tagSet( Tag tag ) const
		{
			auto& simulationTagRegister = context().entityTagRegister();
			const auto simulationTagSet = simulationTagRegister.tagSet( tag );

			// no tag set in simulation context yet, need to check world context
			if( !simulationTagSet )
			{
				return world()->context().entityTagRegister().tagSet( tag );
			}

			return simulationTagSet;
		}

		// Incoming simulation connections.
		std::vector< Guid > incoming;
		// Outgoing simulation connections.
		std::vector< Guid > outgoing;
		// Actions to be performed by agent.
		std::vector< std::shared_ptr< class Action > > actions;
	};

	typedef Property::Variant ArgumentType;
	typedef std::pair< StringHash, ArgumentType > NamedArgument;

	Checksum getChecksum( const std::vector< Guid >& guids )
	{
		Checksum type = NullArguments;
		for( const auto& guid : guids )
		{
			type += static_cast< Checksum >( guid );
		}
		return type;
	}

	Checksum getChecksum( std::vector< Guid >&& guids )
	{
		return getChecksum( guids );
	}

	Checksum getChecksum( const std::unordered_map< StringHash, ArgumentType >& namedArguments )
	{
		Checksum type = NullArguments;
		for( const auto& namedArgument : namedArguments )
		{
			type += static_cast< Checksum >( namedArgument.first );
		}
		return type;
	}

	Checksum getChecksum( std::unordered_map< StringHash, ArgumentType >&& namedArguments )
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

		void add( const NamedArgument newArgument )
		{
			_arguments.emplace( newArgument );
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

		ArgumentType get( std::string_view name ) const
		{
			return get( stringHasher( name.data() ) );
		}

		ArgumentType get( const char* name ) const
		{
			return get( stringHasher( name ) );
		}

		ArgumentType get( const StringHash hash ) const
		{
			for( const auto& argument : _arguments )
			{
				if( argument.first == hash )
				{
					return argument.second;
				}
			}
			return NullGuid;
		}
	};

	// Class representing an action to be performed by an agent.
	// It has a state machine for asyncronous execution.
	class Action
	{
		NamedArguments _arguments{ };

    public:
		Action() = default;
		Action( const Action& other ) : _arguments( other._arguments ) { };
		Action( Action&& other ) : _arguments( std::move( other._arguments ) ) { };
		Action( const NamedArguments& arguments ) : _arguments( arguments ) {  };
		Action( NamedArguments&& arguments ) : _arguments( std::move( arguments ) ) {  };
		~Action() = default;

		virtual std::string_view name() const { return ""; }
		virtual StringHash hash() const { return UndefinedName; }

		NamedArguments& arguments() { return _arguments; }

		enum State : uint8_t
		{
			Waiting,
			Running,
			Paused,
			Aborted,
			Done
		};

		// @Return True when outcome was set property, False otherwise.
		virtual bool outcome( Agent& agent ) { return false; };

		// Cycle of execution of action.
		// @Return Current state of execution.
		virtual State tick( Agent& agent ) { return State::Done; };
	};

	// Simulates an outcome of an action. It alters a simulation object
	// containing current world definition.
	class ActionSimulator
	{
		NamedArguments _arguments{ };

	public:
		ActionSimulator() = delete;
		ActionSimulator( const ActionSimulator& other ) = default;
		ActionSimulator( ActionSimulator&& other ) = default;

		ActionSimulator( const NamedArguments& arguments )
			: _arguments( arguments ) {  };

		ActionSimulator( NamedArguments&& arguments )
			: _arguments( std::move( arguments ) ) {  };

		~ActionSimulator() = default;

		virtual std::string_view name() const { return ""; }
		virtual StringHash hash() const { return UndefinedName; }

		NamedArguments& arguments() { return _arguments; }
		const NamedArguments& arguments() const { return _arguments; }

		// @Return True in case simulation context meets prerequisites, False otherwise.
		virtual bool prerequisites( const Simulation& simulation, const SimAgent& agent, const class Goal& goal ) const { return false; }

		// @Return True if simulation done successfuly, False otherwise.
		virtual bool simulate( Simulation& simulation, SimAgent& agent, const class Goal& goal ) const { return false; }

	};

	// Class representing a specific goal to be achieved by an agent.
	class Goal
	{
	protected:
		World* _world{ nullptr };
		std::vector< Heuristic > _heuristics;

	public:
		Goal() = delete;
		Goal( World& world ) : _world( &world ) { }
		~Goal() = default;

		std::vector< Definition > targets;

		// @Return True if given simulation has reached goal targets, False otherwise.
		bool reached( const Simulation& simulation ) const
		{
			for( auto targetItr = targets.cbegin(); targetItr != targets.cend(); targetItr++ )
			{
				auto ppt = simulation.context().property( targetItr->first );
				if( ppt && ppt->value != targetItr->second )
				{
					return false;
				}
			}
			return true;
		}

		World* world() const { return _world; }

	};

	class ActionSetEntry
	{
	public:
		ActionSetEntry() = default;
		~ActionSetEntry() = default;

		virtual std::string_view name() const { return "None"; }
		virtual StringHash hash() const { return InvalidStringHash; }
		virtual std::shared_ptr< ActionSimulator > simulator( const NamedArguments& arguments ) const { return nullptr; };
		virtual std::shared_ptr< Action > action( const NamedArguments& arguments ) const { return nullptr; };
	};

	// Macro for action set entry class generation.
	// NOTE: action and simulator classes must be
	// previusly declared and have same action name.
#define DEFINE_ACTION_SET_ENTRY( ActionName ) \
	class ActionName##ActionSetEntry : public gie::ActionSetEntry \
	{ \
		using gie::ActionSetEntry::ActionSetEntry; \
		std::string_view name() const override { return #ActionName; } \
		gie::StringHash hash() const override { return gie::stringHasher( #ActionName ); } \
		std::shared_ptr< gie::ActionSimulator > simulator( const gie::NamedArguments& arguments ) const override \
		{ \
			return std::make_shared< ActionName##Simulator >( arguments ); \
		} \
		std::shared_ptr< gie::Action > action( const gie::NamedArguments& arguments ) const override \
		{ \
			return std::make_shared< ActionName##Action >( arguments ); \
		} \
	};

	// GOAP's planner in charge of holding all simulations and 
	// figuring out most effective action path towards goal.
	class Planner
	{
		std::unordered_map< StringHash, std::shared_ptr< ActionSetEntry > > _actionSet;
		std::unordered_map< Guid, Simulation > _simulations;
		std::vector< std::shared_ptr< Action > > _planActions;
		Goal* _goal{ nullptr };
		Agent* _agent{ nullptr };

		// simulation which reached goal
		Guid _goalSimulationGuid{ NullGuid };

		// runs simulations towards goal
		void simulate();

		// backtracks simulations building up plan actions
		void backtrack();

    public:
		Planner() = delete;
		Planner( Goal& goal, Agent& agent ) : _goal( &goal ), _agent( &agent ) { }
		~Planner() = default;

		World* world() const { return _goal->world(); }
		Goal* goal() const { return _goal; }
		Agent* agent() const { return _agent; }
		auto& actionSet() { return _actionSet; }

		template< typename T >
		std::shared_ptr< ActionSetEntry > addActionSetEntry( StringHash hash )
		{
			static_assert( std::is_base_of_v< ActionSetEntry, T >, "Need to be sub class of ActionSetEntry" );
			auto entry = _actionSet.emplace( hash, std::make_shared< T >() );
			return entry.second ? entry.first->second : nullptr;
		}

		std::pair< Guid, Simulation* > createRootSimulation( const Agent* agent )
		{
			Guid newRandGuid{ randGuid() };
			Simulation* newSimulation{ nullptr };
			Simulation sim( newRandGuid, world(), SimAgent( agent ) );
			auto empl = _simulations.emplace( newRandGuid, std::move( sim ) );
			if( empl.second )
			{
				newSimulation = &empl.first->second;
				return { newRandGuid, newSimulation };
			}

			return { NullGuid, nullptr };
		}

		std::pair< Guid, Simulation* > createSimulation( Guid currentSimulationGuid = NullGuid )
		{
			auto currentSimulation = simulation( currentSimulationGuid );
			if( currentSimulationGuid != NullGuid && !currentSimulation )
			{
				return { NullGuid, nullptr };
			}

			Guid newRandGuid{ randGuid() };

			if( currentSimulation )
			{
				Simulation sim( newRandGuid, world(), currentSimulation->context(), currentSimulation->agent() );
				auto empl = _simulations.emplace( newRandGuid, std::move( sim ) );
				if( empl.second )
				{
					auto newSimulation = &empl.first->second;
					newSimulation->incoming.emplace_back( currentSimulationGuid );
					newSimulation->depth = currentSimulation->depth + 1;
					currentSimulation->outgoing.emplace_back( newRandGuid );
					return { newRandGuid, newSimulation };
				}
			}
			
			return { NullGuid, nullptr };
		}

		void deleteSimulation( Guid simulationGuid )
		{
			_simulations.erase( simulationGuid );
		}

		const auto& planActions() const { return _planActions; }
		
		Simulation* simulation( Guid guid )
		{
			if( guid == NullGuid )
			{
				return nullptr;
			}
			auto itr = _simulations.find( guid );
			return ( itr != _simulations.end() ? &( itr->second ) : nullptr );
		}

		const Simulation* simulation( Guid guid ) const
		{
			const auto itr = _simulations.find( guid );
			return ( itr != _simulations.cend() ? &( itr->second ) : nullptr );
		}

		void clearSimulations() { _simulations.clear(); }

		const auto& simulations() { return _simulations; }

		void plan()
		{
			simulate();
			backtrack();
		}

		

	};

	constexpr bool printSteps = true;

	void Planner::simulate()
	{
		// validating world, agent and goal
		if( !agent() || !world() || !goal() )
		{
			return;
		}

		// resetting simulation
		_goalSimulationGuid = NullGuid;
		_simulations.clear();

		// logging
		std::string logContent;
		if( printSteps ) logContent.append( "Planner::simulate() started!\n" );
		if( printSteps ) logContent.append( "Creating root simulation\n" );

		// creating root simulation node
		auto [ rootSimulationGuid, rootSimulation ] = createRootSimulation( agent() );
		if( !rootSimulation )
		{
			return;
		}

		// starting A* by expanding root node
		std::vector< Guid > openedNodes{ rootSimulationGuid };

		// defining max depth to avoid endless expansions
		constexpr size_t depthLimit = 10;

		if( printSteps ) logContent.append( "Starting !openedNodes.empty() while loop\n" );

		while( !openedNodes.empty() )
		{
			// simulation nodes created during this loop,
			// to be added to opened nodes
			std::vector< Guid > expandedNodes{ };

			if( printSteps ) logContent.append( "\nChecking opened nodes.\n" );

			// go through all opened nodes
			for( auto openedNodesItr : openedNodes )
			{
				if( printSteps ) logContent.append( "Getting base simulation.\n" );
				// getting simulation from opened node
				auto baseSimulation = simulation( openedNodesItr );
				if( !baseSimulation )
				{
					continue;
				}

				// cannot expand this node if it has reached simulation depth limit
				if( baseSimulation->depth >= depthLimit )
				{
					if( printSteps ) logContent.append( "Reached max depth, skipping opened node.\n" );
					continue;
				}

				if( printSteps ) logContent.append( "Expanding opened node with available actions.\n" );

				// expanding simulation over all available actions
				for( auto [ _, actionSetEntry ] : _actionSet )
				{
					if( printSteps ) logContent.append( "\nExpanding for Action: " ).append( actionSetEntry->name() ).append( "\n" );

					// getting simulator for action
					auto actionSimulator = actionSetEntry->simulator( { } );
					if( !actionSimulator )
					{
						continue;
					}

					if( printSteps ) logContent.append( "Checking prerequisites.\n" );

					// checking if current simulation context meets action's conditions
					if( !actionSimulator->prerequisites( *baseSimulation, baseSimulation->agent(), *goal() ) )
					{
						if( printSteps ) logContent.append( "Failed on prerequisites, skipping action.\n" );
						continue;
					}

					if( printSteps ) logContent.append( "Creating simulation node for action.\n" );

					// creating new simulation node for action simulation
					auto [ newSimulationGuid, newSimulation ] = createSimulation( openedNodesItr );

					if( printSteps ) logContent.append( "Simulating action.\n" );

					// running action simulation on new node
					bool simulationSuccess = actionSimulator->simulate( *newSimulation, newSimulation->agent(), *goal() );

					// removing new node in case simulation has failed
					if( !simulationSuccess )
					{
						if( printSteps ) logContent.append( "Simulation failed, deleting current simulation node.\n" );
						deleteSimulation( newSimulationGuid );
						continue;
					}

					if( printSteps ) logContent.append( "Simulation successful.\n" );

					// cannot expand this node if it has reached simulation depth limit
					if( newSimulation->depth >= depthLimit )
					{
						if( printSteps ) logContent.append( "Reached max depth, not further expanding current node.\n" );
					}
					else
					{
						if( printSteps ) logContent.append( "Marking node for further expansion.\n" );
						// keeping track of expanded nodes
						expandedNodes.push_back( newSimulationGuid );
					}

					// stopping simulation loop if goal has been reached here
					if( goal()->reached( *newSimulation ) )
					{
						if( printSteps ) logContent.append( "Goal reached!\n" );
						// marking simulation which reached the goal for backtracking
						_goalSimulationGuid = newSimulationGuid;
						// no need to keep expanding nodes
						expandedNodes.clear();
						break;
					}
				}
			}

			// setting next iteration
			openedNodes = expandedNodes;
		}

		std::cout << logContent << std::endl;
		
	}

	void Planner::backtrack()
	{
		// stop if no goal simulation node is assigned
		if( _goalSimulationGuid == NullGuid )
		{
			return;
		}

		// checking if simulation node exists
		auto goalSimulationNode = _simulations.find( _goalSimulationGuid );
		if( goalSimulationNode == _simulations.end() )
		{
			return;
		}

		// cleaning previous planned actions
		_planActions.clear();

		// backtracking from leaf to root simulation nodes
		Guid currentSimulationNode = _goalSimulationGuid;
		while( currentSimulationNode != NullGuid )
		{
			auto simulationNode = _simulations.find( currentSimulationNode );
			if( simulationNode == _simulations.end() )
			{
				break;
			}

			_planActions.insert( _planActions.end(), simulationNode->second.actions.begin(), simulationNode->second.actions.end() );
			
			if( simulationNode->second.incoming.empty() )
			{
				currentSimulationNode = NullGuid;
				break;
			}

			currentSimulationNode = simulationNode->second.incoming.front();
		}

		
	}

}

