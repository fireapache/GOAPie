#pragma once

#include <unordered_map>
#include <vector>
#include <memory>
#include <algorithm>
#include <string>

#include "action.h"
#include "simulation.h"
#include "goal.h"
#include "agent.h"
#include "world.h"

namespace gie
{
	// GOAP's planner in charge of holding all simulations and 
	// figuring out most effective action path towards goal.
	class Planner
	{
		using ActionSetType = std::vector< std::shared_ptr< ActionSetEntry > >;

		ActionSetType _actionSet;
		std::unordered_map< Guid, Simulation > _simulations;
		std::vector< std::shared_ptr< Action > > _planActions;
		Simulation* _rootSimulation{ nullptr };
		Goal* _goal{ nullptr };
		Agent* _agent{ nullptr };
		bool _useHeuristics{ false };
		std::string _logContent{ "" };
		size_t _depthLimit{ 10 };

		// simulation which reached goal
		Guid _goalSimulationGuid{ NullGuid };

		// runs simulations towards goal
		void simulate();

		void expandNode( Simulation* openedNode, std::vector< Simulation* >& expandedNodes );

		// backtracks simulations building up plan actions
		void backtrack();

	public:
		enum class PlanStage : uint8_t
		{
			Idle,
			Simulating,
			Backtracking,
			Done
		};

		enum class SimulateStage : uint8_t
		{
			Idle,
			Initializing,
			ExpandingNode,
			Done
		};

		enum class ExpandNodeStage : uint8_t
		{
			Idle,
			Initializing,
			Expanding,
			Done
		};

	private:
		PlanStage planStage{ PlanStage::Idle };
		SimulateStage simulateStage{ SimulateStage::Idle };
		ExpandNodeStage expandNodeStage{ ExpandNodeStage::Idle };
		bool step{ false };
		bool logSteps{ true };
		std::vector< Simulation* >::iterator openedNodesItr{};
		std::vector< Simulation* > openedNodes{};
		std::vector< Simulation* > newNodes{};
		Simulation* baseSimulation{ nullptr };
		ActionSetType::iterator actionSetItr{};
		std::shared_ptr< ActionSetEntry > actionSetEntry{ nullptr };
		std::shared_ptr< class ActionSimulator > actionSimulator{ nullptr };
		std::pair< Guid, Simulation* > newSimulationPair{ NullGuid, nullptr };

    public:
		Planner() = default;
		Planner( Goal& goal, Agent& agent ) : _goal( &goal ), _agent( &agent ) { }
		~Planner() = default;

		World* world() const { return _goal->world(); }
		Goal* goal() const { return _goal; }
		Agent* agent() const { return _agent; }
		auto& actionSet() { return _actionSet; }
		bool isReady() const { return planStage == PlanStage::Idle || planStage == PlanStage::Done; }
		const Simulation* rootSimulation() const { return _rootSimulation; }
		const std::string& logContent() const { return _logContent; }

		void simulate( Goal& goal, Agent& agent )
		{
			_goal = &goal;
			_agent = &agent;
		}

		template< typename T >
		std::shared_ptr< ActionSetEntry > addActionSetEntry( StringHash hash )
		{
			static_assert( std::is_base_of_v< ActionSetEntry, T >, "Need to be sub class of ActionSetEntry" );
			auto& entry = _actionSet.emplace_back( std::make_shared< T >() );
			return entry;
		}

		std::pair< Guid, Simulation* > createRootSimulation( const Agent* agent );
		std::pair< Guid, Simulation* > createSimulation( Guid currentSimulationGuid = NullGuid );
		void deleteSimulation( Guid simulationGuid );

		const auto& planActions() const { return _planActions; }
		
		Simulation* simulation( Guid guid );
		const Simulation* simulation( Guid guid ) const;

		void clearSimulations() { _simulations.clear(); }

		const auto& simulations() const { return _simulations; }

		void plan( bool useHeuristic = false );

		bool& logStepsMutator() { return logSteps; }
		bool& stepMutator() { return step; }
	};

#define SET_PLAN_STAGE( from, to ) \
	if( planStage == Planner::PlanStage::from ) \
	{ \
		planStage = Planner::PlanStage::to; \
	}

#define SET_SIMULATE_STAGE( from, to ) \
	if( simulateStage == Planner::SimulateStage::from ) \
	{ \
		simulateStage = Planner::SimulateStage::to; \
	}

#define SET_EXPANDNODE_STAGE( from, to ) \
	if( expandNodeStage == Planner::ExpandNodeStage::from ) \
	{ \
		expandNodeStage = Planner::ExpandNodeStage::to; \
	}

	// Inline implementations moved to end to avoid circular dependency issues
	inline std::pair< Guid, Simulation* > Planner::createRootSimulation( const Agent* agent )
	{
		_rootSimulation = nullptr;
		Guid newRandGuid{ randGuid() };
		Simulation* newSimulation{ nullptr };
		Simulation sim( newRandGuid, world(), &world()->context(), SimAgent( agent ) );
		auto empl = _simulations.emplace( newRandGuid, std::move( sim ) );
		if( empl.second )
		{
			newSimulation = &empl.first->second;
			_rootSimulation = newSimulation;
			return { newRandGuid, newSimulation };
		}

		return { NullGuid, nullptr };
	}

	inline std::pair< Guid, Simulation* > Planner::createSimulation( Guid currentSimulationGuid )
	{
		auto currentSimulation = simulation( currentSimulationGuid );
		if( currentSimulationGuid != NullGuid && !currentSimulation )
		{
			return { NullGuid, nullptr };
		}

		Guid newRandGuid{ randGuid() };

		if( currentSimulation )
		{
			Simulation sim( newRandGuid, world(), &currentSimulation->context(), currentSimulation->agent() );
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

	inline void Planner::deleteSimulation( Guid simulationGuid )
	{
		_simulations.erase( simulationGuid );
	}

	inline Simulation* Planner::simulation( Guid guid )
	{
		if( guid == NullGuid )
		{
			return nullptr;
		}
		auto itr = _simulations.find( guid );
		return ( itr != _simulations.end() ? &( itr->second ) : nullptr );
	}

	inline const Simulation* Planner::simulation( Guid guid ) const
	{
		const auto itr = _simulations.find( guid );
		return ( itr != _simulations.cend() ? &( itr->second ) : nullptr );
	}

	inline void Planner::plan( bool useHeuristic )
	{
		SET_PLAN_STAGE( Done, Idle );

		if( planStage == PlanStage::Idle )
		{
			_useHeuristics = useHeuristic;
			planStage = PlanStage::Simulating;
		}

		if( planStage == PlanStage::Simulating )
		{
			simulate();

			if( simulateStage == SimulateStage::Done )
			{
				planStage = PlanStage::Backtracking;
			}
		}

		if( planStage == PlanStage::Backtracking )
		{
			backtrack();
			planStage = PlanStage::Done;
		}
	}

	inline void Planner::simulate()
	{
		SET_SIMULATE_STAGE( Done, Idle );
		SET_SIMULATE_STAGE( Idle, Initializing );

		if( step && simulateStage == SimulateStage::ExpandingNode )
		{
			goto expandNodeLoop;
		}

		// validating world, agent and goal
		if( !agent() || !world() || !goal() )
		{
			return;
		}

		// resetting simulation
		_goalSimulationGuid = NullGuid;
		_simulations.clear();
		_rootSimulation = nullptr;

		// logging
		_logContent = "";
		if( logSteps ) _logContent.append( "Planner::simulate() started!\n" );
		if( logSteps ) _logContent.append( "Creating root simulation\n" );

		// creating root simulation node
		createRootSimulation( agent() );

		if( !_rootSimulation )
		{
			return;
		}

		// starting search by expanding root node
		openedNodes = { _rootSimulation };

		if( logSteps ) _logContent.append( "Starting !openedNodes.empty() while loop.\n" );

		SET_SIMULATE_STAGE( Initializing, ExpandingNode );

		while( !openedNodes.empty() )
		{
			// simulation nodes created during this loop, to be added to opened nodes
			newNodes.clear();

			if( logSteps ) _logContent.append( "Expanding opened nodes.\n" );
			if( _useHeuristics && logSteps ) _logContent.append( "Using heuristics.\n" );

expandNodeLoop:

			openedNodesItr = openedNodes.begin();

			if( _useHeuristics )
			{
				expandNode( *openedNodesItr, newNodes );

				// removing just expanded node from opened nodes
				if( openedNodesItr != openedNodes.end() )
				{
					openedNodes.erase( openedNodesItr );
				}

				if( logSteps ) _logContent.append( "New nodes count: " ).append( std::to_string( newNodes.size() ) ).append( "\n" );

				// adding new nodes to opened nodes
				if( !newNodes.empty() )
				{
					openedNodes.reserve( openedNodes.size() + newNodes.size() );
					openedNodes.insert( openedNodes.end(), newNodes.begin(), newNodes.end() );
				}

				if( logSteps ) _logContent.append( "Sorting nodes.\n" );
				for( auto newNode : newNodes )
				{
					auto insertPos = std::upper_bound( openedNodes.begin(), openedNodes.end(), newNode, &Simulation::smallerThan );
					openedNodes.insert( insertPos, newNode );
				}
				openedNodesItr = openedNodes.begin();
			}
			// go through all opened nodes
			else
			{
				while( openedNodesItr != openedNodes.end() )
				{
					expandNode( *openedNodesItr, newNodes );

					if( step && expandNodeStage != ExpandNodeStage::Done )
					{
						return;
					}

					// checking if last node satified goal
					if( !newNodes.empty() && goal()->reached( *newNodes.back() ) )
					{
						if( logSteps )
							_logContent.append( "Goal reached, stopping simulations!\n" );
						// marking simulation which reached the goal for backtracking
						_goalSimulationGuid = ( *newNodes.end() )->guid();
						// no need to keep expanding nodes
						newNodes.clear();
						openedNodes.clear();
						break;
					}

					openedNodesItr++;
					if( step ) return;
				}
			}

			// setting next iteration
			openedNodes = newNodes;
		}

		if( logSteps ) _logContent.append( "Simulation finished, no more opened nodes.\n" );

		SET_SIMULATE_STAGE( ExpandingNode, Done );
	}

	inline void Planner::expandNode( Simulation* openedNode, std::vector< Simulation* >& newNodes )
	{
		SET_EXPANDNODE_STAGE( Done, Idle );
		SET_EXPANDNODE_STAGE( Idle, Initializing );

		if( step && expandNodeStage == ExpandNodeStage::Expanding )
		{
			goto expandNodeLoop;
		}

		if( logSteps ) _logContent.append( "* Getting base simulation.\n" );
		// getting simulation from opened node
		baseSimulation = openedNode;
		if( !baseSimulation )
		{
			if( logSteps ) _logContent.append( "* No base simulation found!\n" );
			expandNodeStage = ExpandNodeStage::Done;
			return;
		}

		// cannot expand this node if it has reached simulation depth limit
		if( baseSimulation->depth >= _depthLimit )
		{
			if( logSteps ) _logContent.append( "* Reached max depth, skipping opened node.\n" );
			expandNodeStage = ExpandNodeStage::Done;
			return;
		}

		if( logSteps ) _logContent.append( "* Expanding opened node with available actions.\n" );

		actionSetItr = _actionSet.begin();

		SET_EXPANDNODE_STAGE( Initializing, Expanding );
		if( step ) return;

expandNodeLoop:

		// expanding simulation over all available actions
		while( actionSetItr != _actionSet.end() )
		{
			actionSetEntry = *actionSetItr;

			if( logSteps ) _logContent.append( "\n* Expanding for Action: " ).append( actionSetEntry->name() ).append( "\n" );

			// getting simulator for action
			actionSimulator = actionSetEntry->simulator( {} );
			if( !actionSimulator )
			{
				if( logSteps ) _logContent.append( "* Error on instantiating action simulator!\n" );
				actionSetItr++;
				if( step ) return;
				continue;
			}

			if( logSteps ) _logContent.append( "* Checking evaluate.\n" );

			// checking if current simulation context meets action's conditions
			if( !actionSimulator->evaluate( { *baseSimulation, baseSimulation->agent(), *goal() } ) )
			{
				if( logSteps ) _logContent.append( "* Failed on evaluate, skipping action.\n" );
				actionSetItr++;
				if( step ) return;
				continue;
			}

			if( logSteps ) _logContent.append( "* Creating simulation node for action.\n" );

			// creating new simulation node for action simulation
			newSimulationPair = createSimulation( openedNode->guid() );

			if( logSteps ) _logContent.append( "* Simulating action.\n" );

			// running action simulation on new node
			bool simulationSuccess = actionSimulator->simulate( { *newSimulationPair.second, newSimulationPair.second->agent(), *goal() } );

			// removing new node in case simulation has failed
			if( !simulationSuccess )
			{
				if( logSteps ) _logContent.append( "* Simulation failed, deleting current simulation node.\n" );
				deleteSimulation( newSimulationPair.first );
				actionSetItr++;
				if( step ) return;
				continue;
			}

			if( logSteps ) _logContent.append( "* Simulation successful.\n" );

			// cannot expand this node if it has reached simulation depth limit
			if( newSimulationPair.second->depth >= _depthLimit )
			{
				if( logSteps ) _logContent.append( "* Reached max depth, not further expanding current node.\n" );
			}
			else
			{
				if( logSteps ) _logContent.append( "* Marking node for further expansion.\n" );
				// keeping track of expanded nodes
				newNodes.push_back( newSimulationPair.second );
			}

			// stopping simulation loop if goal has been reached here
			if( goal()->reached( *newSimulationPair.second ) )
			{
				if( logSteps ) _logContent.append( "* Goal reached, stopped expanding node!\n" );
				expandNodeStage = ExpandNodeStage::Done;
				break;
			}

			actionSetItr++;
		}

		SET_EXPANDNODE_STAGE( Expanding, Done );
	}

	inline void Planner::backtrack()
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