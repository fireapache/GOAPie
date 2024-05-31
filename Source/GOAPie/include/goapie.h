#pragma once

#include <variant>
#include <unordered_map>
#include <string>

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

		// @return Guid for data entry which this property belongs to.
		Guid ownerGuid() const { return _owner; }
	};

	class DataEntry
	{
		
	public:
		DataEntry( class World* world ) noexcept : _world( world ), _guid( randGuid() ) { }
		DataEntry( const DataEntry& ) noexcept = default;
		DataEntry( DataEntry&& ) noexcept = default;
		~DataEntry() = default;

		Guid guid() const { return _guid; }

		typedef std::pair< Guid, Property* > FetchPropertyResult;

		// Register a property in data entry and world using its literal name.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @return Guid and pointer to property.
		FetchPropertyResult registerProperty( std::string_view name )
		{
			return registerProperty( stringHasher( name ) );
		}

		// Register a property in data entry and world using its name hash.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @return Guid and pointer to property.
		FetchPropertyResult registerProperty( StringHash nameHash );

		// Fetch a property registered in this data entry.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @return Guid and pointer to property.
		FetchPropertyResult property( StringHash nameHash );

		void removeProperty( StringHash name ) { _propertyGuids.erase( name ); }

	private:
		class World* _world;
		Guid _guid{ NullGuid };
		std::unordered_map< StringHash, Guid > _propertyGuids;

	};

	class Blackboard
	{
		class World* _world;
		std::unordered_map< Guid, DataEntry > _entries;

	public:
		Blackboard( class World* world ) : _world( world ) {  };
		~Blackboard() = default;

		// @return Guid to a new data entry.
		Guid registerEntry()
		{
			DataEntry entry{ _world };
			auto result = _entries.emplace( entry.guid(), std::move( entry ) );
			if( result.second )
			{
				return result.first->first;
			}
			return NullGuid;
		}

		// @return Pointer to an existing data entry.
		DataEntry* entry( const Guid guid )
		{
			auto itr = _entries.find( guid );
			if( itr != _entries.end() )
			{
				return &( itr->second );
			}
			return nullptr;
		};

		void removeEntry( const Guid guid ) { _entries.erase( guid ); }
		void clearEntries() { _entries.clear(); }
	};

	class World
	{
		Blackboard _context{ this };
		std::unordered_map< Guid, Property > _properties;

    public:
		World() = default;
		~World() = default;

		auto& context() { return _context; };
		auto& properties() { return _properties; };

	};

	DataEntry::FetchPropertyResult DataEntry::property( StringHash hash )
	{
		if( !_world )
		{
			return { NullGuid, nullptr };
		}

		// getting guid of property
		auto propertyGuid = _propertyGuids.find( hash );

		// property doesn't exist in this data entry
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

	DataEntry::FetchPropertyResult DataEntry::registerProperty( StringHash nameHash )
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

		// erasing property in case its guid could not be registered in data entry
		_world->properties().erase( emplaceResult.first->first );

		return { NullGuid, nullptr };
	}

	class Agent
	{
		Blackboard opinions;

	public:
		Agent() = default;
		~Agent() = default;
	};

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
		World* world{ nullptr };
		std::vector< std::pair< Guid, Property::Variant > > targets;
		std::vector< Heuristic > heuristics;

	public:
		Goal() = delete;
		Goal( World& world ) : world( &world ) { }
		~Goal() = default;

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

