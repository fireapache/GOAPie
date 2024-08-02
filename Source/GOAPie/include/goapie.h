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
		StringHash name() const { return _name; }
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
		// @return Guid to a new (orphan) property.
		// NOTE: it is not assigned to an entity!
		virtual std::pair< Guid, class Property* > createProperty( Guid guid, StringHash name ) = 0;
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

	typedef std::unordered_map< Guid, Entity > EntityMap;
	typedef std::unordered_map< Guid, Property > PropertyMap;

	class Blackboard : public IDataEntityManager
	{
		class World* _world;
		EntityMap _entities;
		PropertyMap _properties;

	public:
		Blackboard( class World* world ) : _world( world ) {  };
		~Blackboard() = default;

		World* world() { return _world; }
		const World* world() const { return _world; }

		EntityMap& entities() { return _entities; }
		const EntityMap& entities() const { return _entities; }

		PropertyMap& properties() { return _properties; }
		const PropertyMap& properties() const { return _properties; }

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

		std::pair< Guid, Property* > createProperty( Guid guid, StringHash name )	override;
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

	std::pair< Guid, Property* > Blackboard::createProperty( Guid guid, StringHash name )
	{
		if( guid == NullGuid )
		{
			guid = randGuid();
		}
		Property property{ guid, NullGuid, name };
		auto result = _properties.emplace( guid, std::move( property ) );
		if( result.second )
		{
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
		void removeProperty( const Guid guid )				override { _context.removeProperty( guid ); };
		void eraseAll()										override { _context.eraseAll(); };
		Property* property( const Guid guid )				override { return  _context.property( guid ); }
		const Property* property( const Guid guid ) const	override { return  _context.property( guid ); }
		Entity* entity( const Guid guid )					override { return  _context.entity( guid ); }
		const Entity* entity( const Guid guid ) const		override { return  _context.entity( guid ); }
		class Agent* agent( const Guid guid )				override { return _context.agent( guid ); }
		const class Agent* agent( const Guid guid ) const	override { return _context.agent( guid ); }

		std::pair< Guid, Property* > createProperty( Guid guid, StringHash name )	override { return _context.createProperty( guid, name ); };

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
		Guid newRandGuid{ randGuid() };
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
		Blackboard _context;

	public:
		Simulation() = delete;
		Simulation( World* world ) : _guid( randGuid() ), _context( world ) { }
		Simulation( Guid guid, World* world ) : _guid( guid ), _context( world ) { }
		Simulation( Simulation&& ) = default;
		~Simulation() = default;

		Guid guid() const { return _guid; }
		const Blackboard& context() const { return _context; }
		Blackboard& context() { return _context; }

		float cost{ MaxCost };
		float heuristic{ MaxHeuristic };
		size_t depth{ 0 };

		// Set property in simulation context.
		// @param guid: property unique identifier
		// @param value: variant value to be assigned to property
		// @return True if property is set successfully, False otherwise
		bool setProperty( Guid guid, Property::Variant value )
		{
			Property* ppt{ nullptr };

			if( ppt = _context.property( guid ) )
			{
				ppt->value = value;
			}
			else if( ppt = _context.world()->property( guid ) )
			{
				auto [ newPptGuid, newPpt ] = _context.createProperty( guid, ppt->name() );
				newPpt->value = value;
				ppt = newPpt;
			}

			return ppt != nullptr;
		}

		enum PropertyContextType : uint8_t
		{
			None,
			Sim,
			World
		};

		struct FetchPropertyResult
		{
			PropertyContextType contextType{ None };
			const Property* property{ nullptr };
		};

		// Get property from either simulation context or world.
		// @param guid: property unique identifier
		// @return Pointer to property if property is set successfully, False otherwise
		FetchPropertyResult getProperty( Guid guid )
		{
			const Property* ppt{ nullptr };

			if( ppt = _context.property( guid ) )
			{
				return { Sim, ppt };
			}
			else if( ppt = _context.world()->property( guid ) )
			{
				auto newPpt = _context.createProperty( guid, ppt->name() );
				return { World, newPpt.second };
			}

			return { None, nullptr };
		}

		// Incoming simulation connections.
		std::vector< Guid > incoming;
		// Outgoing simulation connections.
		std::vector< Guid > outgoing;
		// Actions to be performed by agent.
		std::vector< std::shared_ptr< class Action > > actions;
	};

	Checksum getChecksum( const std::vector< size_t >& values )
	{
		Checksum type = NullArguments;
		for( const auto& name : values )
		{
			type += static_cast< Checksum >( name );
		}
		return type;
	}

	Checksum getChecksum( std::vector< size_t >&& values )
	{
		return getChecksum( values );
	}

	Checksum getChecksum( const std::vector< NamedGuid >& namedGuids )
	{
		Checksum type = NullArguments;
		for( const auto& namedGuid : namedGuids )
		{
			type += static_cast< Checksum >( namedGuid.first );
		}
		return type;
	}

	Checksum getChecksum( std::vector< NamedGuid >&& namedGuids )
	{
		return getChecksum( namedGuids );
	}

	class NamedGuidArguments
	{
		// it's checksum of all string hashes in arguments.
		Checksum _type{ NullArguments };
		std::vector< NamedGuid > _namedGuids;

		void updateType()
		{
			_type = getChecksum( _namedGuids );
		}

	public:

		size_t type() const { return _type; }

		NamedGuidArguments() = default;
		NamedGuidArguments( const std::vector< NamedGuid >& arguments )
			: _namedGuids( arguments )
		{
			updateType();
		};

		NamedGuidArguments( std::vector< NamedGuid >&& arguments )
			: _namedGuids( std::move( arguments ) )
		{
			updateType();
		};

		NamedGuidArguments( const NamedGuidArguments& other )
			: _namedGuids( other._namedGuids ),
			_type( other._type ) { };

		NamedGuidArguments( NamedGuidArguments&& other )
			: _namedGuids( std::move( other._namedGuids ) ),
			_type( other._type ) { };

		~NamedGuidArguments() = default;

		void add( const NamedGuid newArgument )
		{
			_namedGuids.push_back( newArgument );
			updateType();
		}

		void add( const std::vector< NamedGuid >& arguments )
		{
			if( arguments.empty() )
			{
				return;
			}

			_namedGuids.insert( _namedGuids.end(), arguments.begin(), arguments.end() );
			updateType();
		}

		Guid guid( std::string_view name ) const
		{
			return guid( stringHasher( name.data() ) );
		}

		Guid guid( const char* name ) const
		{
			return guid( stringHasher( name ) );
		}

		Guid guid( const StringHash name ) const
		{
			for( const auto& argument : _namedGuids )
			{
				if( argument.first == name )
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
		NamedGuidArguments _arguments{ };

    public:
		Action() = default;
		Action( const Action& other ) : _arguments( other._arguments ) { };
		Action( Action&& other ) : _arguments( std::move( other._arguments ) ) { };
		Action( const NamedGuidArguments& arguments ) : _arguments( arguments ) {  };
		Action( NamedGuidArguments&& arguments ) : _arguments( std::move( arguments ) ) {  };
		~Action() = default;

		virtual StringHash name() const { return UndefinedName; }

		NamedGuidArguments& arguments() { return _arguments; }

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
		NamedGuidArguments _arguments{ };

	public:
		ActionSimulator() = delete;
		ActionSimulator( const ActionSimulator& other ) = default;
		ActionSimulator( ActionSimulator&& other ) = default;

		ActionSimulator( const NamedGuidArguments& arguments )
			: _arguments( arguments ) {  };

		ActionSimulator( NamedGuidArguments&& arguments )
			: _arguments( std::move( arguments ) ) {  };

		~ActionSimulator() = default;

		virtual StringHash name() const { return UndefinedName; }

		NamedGuidArguments& arguments() { return _arguments; }
		const NamedGuidArguments& arguments() const { return _arguments; }

		// @Return True in case simulation context meets prerequisites, False otherwise.
		virtual bool prerequisites( const Simulation& simulation, const Agent& agent ) const { return false; }

		// @Return True if simulation done successfuly, False otherwise.
		virtual bool simulate( Agent& agent, Simulation& simulation ) const { return false; }

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
			auto& properties = simulation.context().properties();
			for( auto targetItr = targets.cbegin(); targetItr != targets.cend(); targetItr++ )
			{
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

	class ActionSetEntry
	{
	public:
		ActionSetEntry() = default;
		~ActionSetEntry() = default;

		virtual StringHash hash() const { return InvalidStringHash; }
		virtual std::shared_ptr< ActionSimulator > simulator( const NamedGuidArguments& arguments ) const { return nullptr; };
		virtual std::shared_ptr< Action > action( const NamedGuidArguments& arguments ) const { return nullptr; };
	};

	// Macro for action set entry class generation.
	// NOTE: action and simulator classes must be
	// previusly declared and have same action name.
#define DEFINE_ACTION_SET_ENTRY( ActionName ) \
	class ActionName##ActionSetEntry : public gie::ActionSetEntry \
	{ \
		using gie::ActionSetEntry::ActionSetEntry; \
		gie::StringHash hash() const override { return gie::stringHasher( #ActionName ); } \
		std::shared_ptr< gie::ActionSimulator > simulator( const gie::NamedGuidArguments& arguments ) const override \
		{ \
			return std::make_shared< ActionName##Simulator >( arguments ); \
		} \
		std::shared_ptr< gie::Action > action( const gie::NamedGuidArguments& arguments ) const override \
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
		std::shared_ptr< ActionSetEntry > addActionSetEntry( StringHash name )
		{
			static_assert( std::is_base_of_v< ActionSetEntry, T >, "Need to be sub class of ActionSetEntry" );
			auto entry = _actionSet.emplace( name, std::make_shared< T >() );
			return entry.second ? entry.first->second : nullptr;
		}

		std::pair< Guid, Simulation* > createSimulation( Guid currentSimulationGuid = NullGuid )
		{
			Guid newRandGuid{ randGuid() };
			auto emplaceResult = _simulations.emplace( newRandGuid, std::move( Simulation{ newRandGuid, world() } ) );
			if( emplaceResult.second )
			{
				auto currentSimulation = simulation( currentSimulationGuid );
				if( currentSimulation )
				{
					emplaceResult.first->second.incoming.emplace_back( currentSimulationGuid );
					emplaceResult.first->second.depth = currentSimulation->depth + 1;
				}
				return { emplaceResult.first->first, &emplaceResult.first->second };
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
			simulate();
			backtrack();
		}

		

	};

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

		// creating root simulation node
		auto [ rootSimulationGuid, rootSimulation ] = createSimulation();
		if( !rootSimulation )
		{
			return;
		}

		// starting A* by expanding root node
		std::vector< Guid > openedNodes{ rootSimulationGuid };

		// defining max depth to avoid endless expansions
		constexpr size_t depthLimit = 10;

		// go through all opened nodes
		auto openedNodesItr = openedNodes.begin();
		while( openedNodesItr != openedNodes.end() )
		{
			// getting simulation from opened node
			auto baseSimulation = simulation( *openedNodesItr );

			for( auto [ _, actionSetEntry ] : _actionSet )
			{
				// getting simulator for action
				auto actionSimulator = actionSetEntry->simulator( { } );
				if( !actionSimulator )
				{
					continue;
				}

				// checking if current simulation context meets action's conditions
				if( !actionSimulator->prerequisites( *baseSimulation, *agent() ) )
				{
					continue;
				}

				// creating new simulation node for action simulation
				auto [ newSimulationGuid, newSimulation ] = createSimulation( *openedNodesItr );

				// running action simulation on new node
				bool simulationSuccess = actionSimulator->simulate( *agent(), *newSimulation );

				// removing new node in case simulation has failed
				if( !simulationSuccess )
				{
					deleteSimulation( newSimulationGuid );
					continue;
				}

				// stopping simulation loop if goal has been reached here
				if( goal()->reached( *newSimulation ) )
				{
					_goalSimulationGuid = newSimulationGuid;
					openedNodesItr = openedNodes.end();
					break;
				}

				// adding new simulation node as opened node for further expansion
				if( newSimulation->depth < depthLimit )
				{
					openedNodes.push_back( newSimulationGuid );
				}
			}

			// getting next opened node
			if( openedNodesItr != openedNodes.end() )
			{
				openedNodesItr = openedNodes.erase( openedNodesItr );
			}
		}
		
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

