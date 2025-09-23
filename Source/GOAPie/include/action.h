#pragma once

#include <string_view>
#include <memory>

#include "arguments.h"
#include "common.h"

namespace gie
{
// Forward declarations
class Agent;
class SimAgent;
class Goal;
class Simulation;
class DebugMessages;

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

		virtual std::string_view name() const { return stringRegister().get( hash() ); }
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

		// Cycle of execution of action.
		// @Return Current state of execution.
		virtual State tick( Agent& agent ) { return State::Done; };
	};

struct EvaluateSimulationParams
{
private:
// Debug messages for simulation.
DebugMessages* debugMessages;
// Named arguments for simulation.
NamedArguments& namedArguments;

public:
// Agent which is performing action.
const SimAgent& agent;
// Simulation context.
const Simulation& simulation;
// Goal which is being achieved.
const Goal& goal;

// Constructor declaration only - implementation deferred
EvaluateSimulationParams( Simulation& simulation, const SimAgent& agent, const Goal& goal );

void addDebugMessage( std::string_view message );
NamedArguments& arguments();
};

struct SimulateSimulationParams
{
public:
// Agent which is performing action.
SimAgent& agent;
// Simulation context.
Simulation& simulation;
// Goal which is being achieved.
const Goal& goal;

// Constructor declaration only - implementation deferred
SimulateSimulationParams( Simulation& simulation, SimAgent& agent, const Goal& goal );

void addDebugMessage( std::string_view message );
NamedArguments& arguments();
};

	struct CalculateHeuristicParams : public SimulateSimulationParams
	{
		using SimulateSimulationParams::SimulateSimulationParams;
	};

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

		std::string_view name() const { return stringRegister().get( hash() ); }
		virtual StringHash hash() const { return UndefinedName; }

		NamedArguments& arguments() { return _arguments; }
		const NamedArguments& arguments() const { return _arguments; }

		// @Return True in case context meets prerequisites, False otherwise.
		virtual bool evaluate( EvaluateSimulationParams params ) const { return false; }

		// @Return True if simulation setup was done successfuly, False otherwise.
		virtual bool simulate( SimulateSimulationParams params ) const { return false; }

		// Calculates heuristc value for simulation.
		virtual void calculateHeuristic( CalculateHeuristicParams params ) const {}
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

	// Macro for action class generation.
#define DEFINE_DUMMY_ACTION_CLASS( ActionName ) \
	class ActionName##Action : public gie::Action \
	{ \
		using gie::Action::Action; \
		gie::StringHash hash() const override{ return gie::stringHasher( #ActionName ); } \
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

// Implementations - must be after all forward declarations are resolved
// These should be included after simulation.h is included in the translation unit

inline EvaluateSimulationParams::EvaluateSimulationParams( 
    Simulation& simulation, 
    const SimAgent& agent, 
    const Goal& goal )
: simulation( simulation )
, agent( agent )
, goal( goal )
, debugMessages( nullptr )  // Will be set when simulation.h is available
, namedArguments( *reinterpret_cast<NamedArguments*>(nullptr) )  // Will be set when simulation.h is available
{
    // Note: This implementation is incomplete - the actual initialization
    // should be done when the full Simulation type is available
}

inline void EvaluateSimulationParams::addDebugMessage( std::string_view message )
{
    // This will need to be implemented when DebugMessages is fully defined
}

inline NamedArguments& EvaluateSimulationParams::arguments()
{
    return namedArguments;
}

inline SimulateSimulationParams::SimulateSimulationParams( 
    Simulation& simulation, 
    SimAgent& agent, 
    const Goal& goal )
: simulation( simulation )
, agent( agent )
, goal( goal )
{
}

inline void SimulateSimulationParams::addDebugMessage( std::string_view message )
{
    // This will need to be implemented when Simulation is fully defined
}

inline NamedArguments& SimulateSimulationParams::arguments()
{
    // This will need to be implemented when Simulation is fully defined
    return *reinterpret_cast<NamedArguments*>(nullptr);
}

}
