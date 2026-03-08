#pragma once

#include <string_view>
#include <memory>
#include <functional>

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

	// Lambda function type aliases for inline action definitions.
	struct EvaluateSimulationParams;
	struct SimulateSimulationParams;
	struct CalculateHeuristicParams;
	using EvaluateFunc = std::function< bool( EvaluateSimulationParams ) >;
	using SimulateFunc = std::function< bool( SimulateSimulationParams ) >;
	using HeuristicFunc = std::function< void( CalculateHeuristicParams ) >;

	// Class representing an action produced by the planner.
	class Action
	{
		StringHash _hash{ UndefinedName };
		NamedArguments _arguments{ };

    public:
		Action() = default;
		Action( const Action& other ) : _hash( other._hash ), _arguments( other._arguments ) { };
		Action( Action&& other ) : _hash( other._hash ), _arguments( std::move( other._arguments ) ) { };
		Action( const NamedArguments& arguments ) : _arguments( arguments ) {  };
		Action( NamedArguments&& arguments ) : _arguments( std::move( arguments ) ) {  };
		Action( StringHash hash, const NamedArguments& arguments = {} ) : _hash( hash ), _arguments( arguments ) {};
		~Action() = default;

		virtual std::string_view name() const { return stringRegister().get( hash() ); }
		virtual StringHash hash() const { return _hash; }

		NamedArguments& arguments() { return _arguments; }
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

		EvaluateSimulationParams(
			Simulation& simulation,
			const SimAgent& agent,
			const Goal& goal )
			: simulation( simulation )
			, agent( agent )
			, goal( goal )
			, debugMessages( &simulation.debugMessages() )
			, namedArguments( simulation.arguments() )
		{
		}

		void addDebugMessage( std::string_view message )
		{
			if( debugMessages )
			{
				debugMessages->add( message );
			}
		}

		NamedArguments& arguments()
		{
			return namedArguments;
		}
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
		SimulateSimulationParams(
			Simulation& simulation,
			SimAgent& agent,
			const Goal& goal )
			: simulation( simulation )
			, agent( agent )
			, goal( goal )
		{
		}

		void addDebugMessage( std::string_view message )
		{
			simulation.debugMessages().add( message );
		}

		NamedArguments& arguments()
		{
			return simulation.arguments();
		}
	};

	struct CalculateHeuristicParams : public SimulateSimulationParams
	{
		using SimulateSimulationParams::SimulateSimulationParams;
	};

	// Action simulator used by the planner during search.
	// Supports two usage styles:
	//   1. Subclass and override evaluate/simulate/calculateHeuristic (class-based)
	//   2. Construct with lambda functions via the constructor (lambda-based)
	// When lambdas are set, they take priority over the virtual defaults.
	class ActionSimulator
	{
		StringHash _hash{ UndefinedName };
		NamedArguments _arguments{ };
		bool _forceLeaf{ false };
		EvaluateFunc _evaluateFn;
		SimulateFunc _simulateFn;
		HeuristicFunc _heuristicFn;

	public:
		ActionSimulator() = delete;
		ActionSimulator( const ActionSimulator& other ) = default;
		ActionSimulator( ActionSimulator&& other ) = default;

		ActionSimulator( const NamedArguments& arguments )
			: _arguments( arguments ) {  };

		ActionSimulator( NamedArguments&& arguments )
			: _arguments( std::move( arguments ) ) {  };

		// Lambda-based constructor.
		ActionSimulator(
			StringHash hash,
			const NamedArguments& arguments,
			EvaluateFunc evaluateFn = {},
			SimulateFunc simulateFn = {},
			HeuristicFunc heuristicFn = {},
			bool forceLeaf = false )
			: _hash( hash )
			, _arguments( arguments )
			, _forceLeaf( forceLeaf )
			, _evaluateFn( std::move( evaluateFn ) )
			, _simulateFn( std::move( simulateFn ) )
			, _heuristicFn( std::move( heuristicFn ) )
		{
		}

		~ActionSimulator() = default;

		std::string_view name() const { return stringRegister().get( hash() ); }
		virtual StringHash hash() const { return _hash; }

		NamedArguments& arguments() { return _arguments; }
		const NamedArguments& arguments() const { return _arguments; }

		bool forceLeaf() const { return _forceLeaf; }
		void setForceLeaf( bool value ) { _forceLeaf = value; }

		// @Return True in case context meets prerequisites, False otherwise.
		virtual bool evaluate( EvaluateSimulationParams params ) const
		{
			return _evaluateFn ? _evaluateFn( std::move( params ) ) : false;
		}

		// @Return True if simulation setup was done successfuly, False otherwise.
		virtual bool simulate( SimulateSimulationParams params ) const
		{
			return _simulateFn ? _simulateFn( std::move( params ) ) : false;
		}

		// Calculates heuristc value for simulation.
		virtual void calculateHeuristic( CalculateHeuristicParams params ) const
		{
			if( _heuristicFn ) _heuristicFn( std::move( params ) );
		}
	};

	// Factory that produces ActionSimulator and Action instances for the planner.
	// Supports two usage styles:
	//   1. Subclass and override simulator/action (class-based, used by macros)
	//   2. Construct with lambda functions (lambda-based, used by addLambdaAction)
	class ActionSetEntry
	{
		StringHash _hash{ InvalidStringHash };
		EvaluateFunc _evaluateFn;
		SimulateFunc _simulateFn;
		HeuristicFunc _heuristicFn;
		bool _forceLeaf{ false };

	public:
		ActionSetEntry() = default;
		~ActionSetEntry() = default;

		// Lambda-based constructor.
		ActionSetEntry(
			StringHash hash,
			EvaluateFunc evaluateFn,
			SimulateFunc simulateFn,
			HeuristicFunc heuristicFn = {},
			bool forceLeaf = false )
			: _hash( hash )
			, _evaluateFn( std::move( evaluateFn ) )
			, _simulateFn( std::move( simulateFn ) )
			, _heuristicFn( std::move( heuristicFn ) )
			, _forceLeaf( forceLeaf )
		{
		}

		virtual std::string_view name() const
		{
			return _hash != InvalidStringHash ? stringRegister().get( _hash ) : "None";
		}

		virtual StringHash hash() const { return _hash; }

		virtual std::shared_ptr< ActionSimulator > simulator( const NamedArguments& arguments ) const
		{
			if( _evaluateFn || _simulateFn )
			{
				return std::make_shared< ActionSimulator >(
					_hash, arguments, _evaluateFn, _simulateFn, _heuristicFn, _forceLeaf );
			}
			return nullptr;
		}

		virtual std::shared_ptr< Action > action( const NamedArguments& arguments ) const
		{
			if( _hash != InvalidStringHash )
			{
				return std::make_shared< Action >( _hash, arguments );
			}
			return nullptr;
		}
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
}