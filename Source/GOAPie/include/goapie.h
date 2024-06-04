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
			glm::vec3,
			void* > Variant;

		Variant value;

		std::pair< bool, bool >			getBool()		const { return std::pair{ type() == Boolean, std::get< bool >( value ) }; }
		std::pair< bool, float >		getFloat()		const { return std::pair{ type() == Float, std::get< float >( value ) }; }
		std::pair< bool, int32_t >		getInteger()	const { return std::pair{ type() == Integer, std::get< int32_t >( value ) }; }
		std::pair< bool, StringHash >	getStringHash()	const { return std::pair{ type() == String, std::get< StringHash >( value ) }; }
		std::pair< bool, ArrayType >	getArray()		const { return std::pair{ type() == Array, std::get< ArrayType >( value ) }; }
		std::pair< bool, glm::vec3 >	getVec3()		const { return std::pair{ type() == Vec3, std::get< glm::vec3 >( value ) }; }
		std::pair< bool, void* >		getCustom()		const { return std::pair{ type() == Custom, std::get< void* >( value ) }; }

		enum Type : uint8_t
		{
			Unknow,
			Boolean,
			Float,
			Integer,
			String,
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
			if( std::holds_alternative< StringHash >	( value ) ) return String;
			if( std::holds_alternative< ArrayType >		( value ) )	return Array;
			if( std::holds_alternative< glm::vec3 >		( value ) )	return Vec3;
			if( std::holds_alternative< void* >			( value ) )	return Custom;
		};

		// @return Guid for this property.
		Guid guid() const { return _guid; };

		// @return Guid for data entity which this property belongs to.
		Guid ownerGuid() const { return _owner; }
	};

	typedef std::pair< Guid, Property::Variant > Definition;
	typedef std::pair< StringHash, Property::Variant > NamedDefinition;

	class Entity
	{
		
	public:
		Entity() = delete;
		Entity( class World* world ) noexcept : _world( world ), _guid( randGuid() ) { }
		Entity( const Entity& ) noexcept = default;
		Entity( Entity&& ) noexcept = default;
		~Entity() = default;

		Guid guid() const { return _guid; }

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

		void removeProperty( StringHash name ) { _propertyGuids.erase( name ); }

		class World* world() const { return _world; }


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
		virtual std::pair< Guid, class Entity* > createEntity() = 0;
		virtual void removeEntity( const Guid guid ) = 0;
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

	class Blackboard : public IDataEntityManager
	{
		class World* _world;
		std::unordered_map< Guid, Entity > _entities;
		std::unordered_map< Guid, Property > _properties;

	public:
		Blackboard( class World* world ) : _world( world ) {  };
		~Blackboard() = default;

		std::unordered_map< Guid, Entity >& entities() { return _entities; }
		const std::unordered_map< Guid, Entity >& entities() const { return _entities; }

		std::unordered_map< Guid, Property >& properties() { return _properties; }
		const std::unordered_map< Guid, Property >& properties() const { return _properties; }

		// IDataEntityManager interface
		std::pair< Guid, class Agent* > createAgent()			override;
		std::pair< Guid, Entity* > createEntity()				override;
		Property* property( const Guid guid )					override;
		const Property* property( const Guid guid )	const		override;
		Entity* entity( const Guid guid )						override;
		const Entity* entity( const Guid guid ) const			override;
		class Agent* agent( const Guid guid )					override;
		const class Agent* agent( const Guid guid ) const		override;
		void removeAgent( const Guid guid )						override { removeEntity( guid ); }
		void removeEntity( const Guid guid )					override { _entities.erase( guid ); }
		void eraseAll()											override { _entities.clear(); }
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

	Property* Blackboard::property( const Guid guid )
	{
		auto itr = _properties.find( guid );
		if( itr != _properties.end() )
		{
			return &( itr->second );
		}
		return nullptr;
	}

	const Property* Blackboard::property( const Guid guid ) const
	{
		auto itr = _properties.find( guid );
		if( itr != _properties.end() )
		{
			return &( itr->second );
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

	class World : public IDataEntityManager
	{
		Blackboard _context{ this };

    public:
		World() = default;
		~World() = default;

		auto& context() { return _context; };
		auto& properties() { return _context.properties(); };

		// IDataEntityManager interface
		std::pair< Guid, class Agent* > createAgent()		override { return _context.createAgent(); };
		void removeAgent( const Guid guid )					override { _context.removeAgent( guid ); };
		std::pair< Guid, Entity* > createEntity()			override { return _context.createEntity(); };
		void removeEntity( const Guid guid )				override { _context.removeEntity( guid ); };
		void eraseAll()										override { _context.eraseAll(); };
		Property* property( const Guid guid )				override { return  _context.property( guid ); }
		const Property* property( const Guid guid ) const	override { return  _context.property( guid ); }
		Entity* entity( const Guid guid )					override { return  _context.entity( guid ); }
		const Entity* entity( const Guid guid ) const		override { return  _context.entity( guid ); }
		class Agent* agent( const Guid guid )				override { return _context.agent( guid ); }
		const class Agent* agent( const Guid guid ) const	override { return _context.agent( guid ); }

	};

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
		Guid newRandGuid{ gie::randGuid() };
		auto emplaceResult = _world->properties().emplace( newRandGuid, std::move( Property{ newRandGuid, _guid, nameHash } ) );

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

	class Agent : public Entity
	{
	public:
		Agent() = delete;
		Agent( World* world ) : Entity::Entity( world ), opinions( world ) { };
		Agent( const Agent& agent ) = default;
		Agent( Agent&& agent ) = default;
		~Agent() = default;

		Blackboard opinions;
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

	public:
		Simulation() = delete;
		Simulation( World* world ) : _guid( randGuid() ), context( world ) { }
		Simulation( Guid guid, World* world ) : _guid( guid ), context( world ) { }
		Simulation( Simulation&& ) = default;
		~Simulation() = default;

		Guid guid() const { return _guid; }

		float cost{ MaxCost };
		float heuristic{ MaxHeuristic };

		Blackboard context;

		// Incoming simulation connections.
		std::vector< Guid > incoming;
		// Outgoing simulation connections.
		std::vector< Guid > outgoing;
		// Actions to be performed by agent.
		//std::vector< Action > actions;
	};

	// Class representing a performable action. It outputs a simulation object
	// containing all info for planner's A* simulation graph, including cost,
	// heuristics and simulation outcomes.
	class ActionSimulation
	{
		class Planner* _planner;

	public:
		ActionSimulation( Planner& planner ) : _planner( &planner ) {  };
		~ActionSimulation() = default;

		virtual StringHash actionName() const { return UndefinedActionName; }

		// @Return True in case context meets prerequisites, False otherwise.
		virtual bool prerequisites( const Simulation& context, const Agent& agent ) const { return false; }

		// @Return Guid and pointer to new valid simulation, both null if simulation fails.
		virtual bool simulate( const Simulation& base ) const { return false; }

		Planner* planner() const { return _planner; }

	};

	// Class representing an action to be performed by an agent.
	// It has a state machine for asyncronous execution.
	class Action
	{
    public:
		Action() = default;
		~Action() = default;

		virtual StringHash actionName() const { return UndefinedActionName; }

		enum State : uint8_t
		{
			Waiting,
			Running,
			Paused,
			Aborted,
			Done
		};

		// @Return True when outcome was set property, False otherwise.
		virtual bool outcome( Agent& agent ) {  };

		// Cycle of execution of action.
		// @Return Current state of execution.
		virtual State tick( Agent& agent ) { return State::Aborted; };
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
				auto& properties = simulation.context.properties();
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

		World* world() const { return _world; }

	};

	// GOAP's planner in charge of holding all simulations and 
	// figuring out most effective action path towards goal.
	class Planner
	{
		std::unordered_map< Guid, Action > _actionSet;
		std::unordered_map< Guid, Simulation > _simulations;
		std::vector< Action > _plannedActions;
		Goal* _goal{ nullptr };
		Agent* _agent{ nullptr };

    public:
		Planner() = delete;
		Planner( Goal& goal, Agent& agent ) : _goal( &goal ), _agent( &agent ) { }
		~Planner() = default;

		World* world() const { return _goal->world(); }
		Goal* goal() const { return _goal; }
		Agent* agent() const { return _agent; }

		std::pair< Guid, Simulation* > createSimulation( Guid currentSimulationGuid = NullGuid )
		{
			Guid newRandGuid{ gie::randGuid() };
			auto emplaceResult = _simulations.emplace( newRandGuid, std::move( Simulation{ newRandGuid, world() } ) );
			if( emplaceResult.second )
			{
				if( currentSimulationGuid != NullGuid )
				{
					emplaceResult.first->second.incoming.emplace_back( currentSimulationGuid );
				}
				return { emplaceResult.first->first, &emplaceResult.first->second };
			}
			return { NullGuid, nullptr };
		}

		const std::vector< Action >& plannedActions() const { return _plannedActions; }
		
		Simulation* simulation( Guid guid )
		{
			auto itr = _simulations.find( guid );
			return ( itr != _simulations.end() ? &( itr->second ) : nullptr );
		}

		const Simulation* simulation( Guid guid ) const
		{
			const auto itr = _simulations.find( guid );
			return ( itr != _simulations.cend() ? &( itr->second ) : nullptr );
		}

		void clearSimulations() { _simulations.clear(); }

		void plan()
		{

		}

	};

}

