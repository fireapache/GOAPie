#pragma once

#include <vector>
#include <memory>
#include <set>

#include "blackboard.h"
#include "agent.h"
#include "arguments.h"
#include "goal.h"
#include "common.h"

namespace gie
{
	// Forward declarations
	class World;
	class Action;

#if defined( GIE_DEBUG )
	class DebugMessages
	{
		std::vector< std::string > _messages;

	public:
		DebugMessages() = default;
		DebugMessages( const DebugMessages& ) = default;
		DebugMessages( DebugMessages&& ) = default;
		~DebugMessages() = default;
		void add( std::string_view message )
		{
			_messages.emplace_back( message );
		}
		const std::vector< std::string >* messages() const
		{
			return &_messages;
		}
	};
#else
	class DebugMessages
	{
		public:
		DebugMessages() = default;
		DebugMessages( const DebugMessages& ) = default;
		DebugMessages( DebugMessages&& ) = default;
		~DebugMessages() = default;
		void add( std::string_view ) { }
		const std::vector< std::string >* messages() const { return nullptr; }
	};
#endif

	// Class representing a simulation of a performable action.
	// It stores all information necessary for planner's A* algorithm.
	// It also stores a list of actions to be performed by the agent,
	// in case the simulation is chosed as a valid goal path.
	class Simulation
	{
		friend class Planner;

		Guid _guid{ NullGuid };
		Blackboard _context;
		World* _world{ nullptr };
		SimAgent _simAgent;
		NamedArguments _arguments;
		DebugMessages _debugMessages;

	public:
		Simulation() = delete;
		Simulation( Guid guid, World* world, const Blackboard* parentContext, const SimAgent& simAgent )
			: _world( world ),
			_guid( guid ),
			_context( world, parentContext ),
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

		DebugMessages& debugMessages() { return _debugMessages; }
		const DebugMessages& debugMessages() const { return _debugMessages; }

		NamedArguments& arguments() { return _arguments; }
		const NamedArguments& arguments() const { return _arguments; }

		float cost{ MaxCost };
		Heuristic::Result heuristic{ InvalidHeuristic };
		size_t depth{ 0 };

		// comparison operator for std sorting
		bool operator<( const Simulation& other ) const
		{
			return ( cost + heuristic.value ) < ( other.cost + other.heuristic.value );
		}

		// for std sorting
		static bool smallerThan( const Simulation* lhs, const Simulation* rhs )
		{
			return *lhs < *rhs;
		}

		// Return set of entities with tag
		const std::set< Guid >* tagSet( std::string_view tagName ) const
		{
			return tagSet( stringHasher( tagName ) );
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
}