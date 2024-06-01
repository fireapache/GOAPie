#pragma once

#include <set>
#include <string>
#include <unordered_map>
#include <variant>

#include "common.h"

namespace gie
{
	class Property
	{
		Guid _guid{ NullGuid };
		Guid _owner{ NullGuid };
		StringHash _name{ InvalidStringHash };

	public:
		Property( Guid guid, Guid owner, StringHash name ) : _guid( guid ), _owner( owner ), _name( name ) { }
		Property( Guid owner, StringHash name ) : _guid( randGuid() ), _owner( owner ), _name( name ) { }
		Property() = delete;
		Property( const Property& ) noexcept = default;
		Property( Property&& ) noexcept = default;
		~Property() = default;

		typedef std::vector< Guid > ArrayType;

		typedef std::variant<
			bool,
			float,
			int32_t,
			StringHash,
			ArrayType,
			void* > Variant;

		Variant value;

		//void operator=( const auto&& v ) { value.emplace( v ); }

		enum Type : uint8_t
		{
			Unknow,
			Boolean,
			Float,
			Integer,
			String,
			Array,
			Custom
		};

		// @return Type of data being stored in this property.
		Type type() const
		{
			if( std::holds_alternative< bool >			( value ) )	return Boolean;
			if( std::holds_alternative< float >			( value ) )	return Float;
			if( std::holds_alternative< int32_t >		( value ) )	return Integer;
			if( std::holds_alternative< StringHash >	( value ) ) return String;
			if( std::holds_alternative< ArrayType >		( value ) )	return Array;
			if( std::holds_alternative< void* >			( value ) )	return Custom;
		};

		// @return Guid for this property.
		Guid guid() const { return _guid; };

		// @return Guid for data entity which this property belongs to.
		Guid ownerGuid() const { return _owner; }
	};

	class DataEntity
	{
		
	public:
		DataEntity() = delete;
		DataEntity( class World* world ) noexcept : _world( world ), _guid( randGuid() ) { }
		DataEntity( const DataEntity& ) noexcept = default;
		DataEntity( DataEntity&& ) noexcept = default;
		~DataEntity() = default;

		Guid guid() const { return _guid; }

		typedef std::pair< Guid, Property* > FetchPropertyResult;

		// Register a property in data entity and world using its literal name.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @return Guid and pointer to property.
		FetchPropertyResult createProperty( std::string_view name )
		{
			return createProperty( stringHasher( name ) );
		}

		// Register a property in data entity and world using its name hash.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @return Guid and pointer to property.
		FetchPropertyResult createProperty( StringHash nameHash );

		// Fetch a property registered in this data entity.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @return Guid and pointer to property.
		FetchPropertyResult property( StringHash nameHash );

		void removeProperty( StringHash name ) { _propertyGuids.erase( name ); }

	protected:
		class World* _world;
		Guid _guid{ NullGuid };
		std::unordered_map< StringHash, Guid > _propertyGuids;

	};

	class IDataEntityManager
	{
	public:
		// @return Guid to a new agent entity.
		virtual std::pair< Guid, class Agent* > createAgent() = 0;
		virtual void removeAgent( const Guid guid ) = 0;
		// @return Guid to a new data entity.
		virtual std::pair< Guid, class DataEntity* > createEntity() = 0;
		virtual void removeEntity( const Guid guid ) = 0;
		virtual void eraseAll() = 0;
		// @return Pointer to an existing data entity.
		virtual DataEntity* entity( const Guid guid ) = 0;
		// @return Pointer to an existing agent.
		virtual class Agent* agent( const Guid guid ) = 0;
	};

	class Blackboard : public IDataEntityManager
	{
		class World* _world;
		std::unordered_map< Guid, DataEntity > _entries;

	public:
		Blackboard( class World* world ) : _world( world ) {  };
		~Blackboard() = default;

		// IDataEntityManager interface
		std::pair< Guid, class Agent* > createAgent()			override;
		std::pair< Guid, DataEntity* > createEntity()		override;
		DataEntity* entity( const Guid guid )					override;
		class Agent* agent( const Guid guid )					override;
		void removeAgent( const Guid guid )						override { removeEntity( guid ); }
		void removeEntity( const Guid guid )					override { _entries.erase( guid ); }
		void eraseAll()											override { _entries.clear(); }
	};

	std::pair< Guid, DataEntity* > Blackboard::createEntity()
	{
		DataEntity entity{ _world };
		auto result = _entries.emplace( entity.guid(), std::move( entity ) );
		if( result.second )
		{
			return std::pair{ result.first->first, &result.first->second };
		}
		return std::pair{ NullGuid, nullptr };
	}

	DataEntity* Blackboard::entity( const Guid guid ) 
	{
		auto itr = _entries.find( guid );
		if( itr != _entries.end() )
		{
			return &( itr->second );
		}
		return nullptr;
	}

	class World : public IDataEntityManager
	{
		Blackboard _context{ this };
		std::unordered_map< Guid, Property > _properties;

    public:
		World() = default;
		~World() = default;

		auto& context() { return _context; };
		auto& properties() { return _properties; };

		// IDataEntityManager interface
		std::pair< Guid, class Agent* > createAgent()	override { return _context.createAgent(); };
		void removeAgent( const Guid guid )				override { _context.removeAgent( guid ); };
		std::pair< Guid, DataEntity* > createEntity()	override { return _context.createEntity(); };
		void removeEntity( const Guid guid )			override { _context.removeEntity( guid ); };
		void eraseAll()									override { _context.eraseAll(); };
		DataEntity* entity( const Guid guid )			override { return  _context.entity( guid ); }
		class Agent* agent( const Guid guid )			override { return _context.agent( guid ); }

	};

	DataEntity::FetchPropertyResult DataEntity::property( StringHash hash )
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

		// removing guid from register if property doesn't exist
		if( propertyItr == _world->properties().end() )
		{
			_propertyGuids.erase( hash );
			return { NullGuid, nullptr };
		}

		return { propertyItr->first, &( propertyItr->second ) };
	}

	DataEntity::FetchPropertyResult DataEntity::createProperty( StringHash nameHash )
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
		Guid newRandGuid{ gie::randGuid() };
		auto emplaceResult = _world->properties().emplace( gie::randGuid(), std::move( Property{ newRandGuid, _guid, nameHash } ) );

		if( !emplaceResult.second )
		{
			return { NullGuid, nullptr };
		}

		// mapping property name to property guid
		auto registeredPropertyGuid = _propertyGuids.emplace( nameHash, emplaceResult.first->first );
		if( registeredPropertyGuid.second )
		{
			return { emplaceResult.first->first, &( emplaceResult.first->second ) };
		}

		// erasing property in case its guid could not be registered in data entity
		_world->properties().erase( emplaceResult.first->first );

		return { NullGuid, nullptr };
	}

	class Agent : public DataEntity
	{
	public:
		Agent() = delete;
		Agent( World* world ) : DataEntity::DataEntity( world ), opinions( world ) { };
		Agent( const Agent& agent ) = default;
		Agent( Agent&& agent ) = default;
		~Agent() = default;

		Blackboard opinions;
	};

	Agent* Blackboard::agent( const Guid guid )
	{
		DataEntity* dataEntity = entity( guid );
		return static_cast< Agent* >( dataEntity );
	}

	std::pair< Guid, Agent* > Blackboard::createAgent()
	{
		Agent agent{ _world };
		auto result = _entries.emplace( agent.guid(), std::move( agent ) );
		if( result.second )
		{
			return std::pair{ result.first->first, static_cast< Agent* >( &result.first->second ) };
		}
		return std::pair{ NullGuid, nullptr };
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

	class Simulation
	{
	protected:
		Blackboard result;

	public:
		Simulation() = default;
		~Simulation() = default;

		float cost{ MaxCost };
		std::vector< Heuristic::Result > heuristics;
	};

	class AvailableAction
	{
	public:
		AvailableAction() = default;
		~AvailableAction() = default;

	};

	class Action
	{
    public:
		Action() = default;
		~Action() = default;

		enum State : uint8_t
		{
			Waiting,
			Running,
			Aborted,
			Done
		};

		virtual State execute() { return State::Aborted; };
	};

	class Goal
	{
	protected:
		World* _world{ nullptr };
		std::vector< Heuristic > _heuristics;

	public:
		Goal() = delete;
		Goal( World& world ) : _world( &world ) { }
		~Goal() = default;

		std::unordered_map< Guid, Property::Variant > targets;

		bool done() const
		{
			for( auto targetItr = targets.cbegin(); targetItr != targets.cend(); targetItr++ )
			{
				auto& properties = _world->properties();
				const auto propertyItr = properties.find( targetItr->first );
				if( propertyItr != properties.cend() )
				{
					if( propertyItr->second.value != targetItr->second )
					{
						return false;
					}
				}
			}
			return true;
		}

	};

	class Planner
	{
    protected:
		std::vector< AvailableAction > actionSet;
		std::vector< Action > plannedActions;
		World* world{ nullptr };
		Goal* goal{ nullptr };

    public:
		Planner() = delete;
		Planner( World& world, Goal& goal ) : world( &world ), goal( &goal ) { }
		~Planner() = default;


	};

}

