#include <goapie.h>

void printSimulatedActions( const gie::Planner& planner )
{
	std::vector< gie::Guid > leafSimulationGuids{};
	auto& simulations = planner.simulations();
	for( auto& simulationItr : simulations )
	{
		if( simulationItr.second.outgoing.empty() )
		{
			leafSimulationGuids.push_back( simulationItr.first );
		}
	}

	for( auto leafSimulationGuid : leafSimulationGuids )
	{
		std::vector< std::string_view > actionNames;
		auto simulationItr = simulations.find( leafSimulationGuid );

		// iterating until root simulation
		while( !simulationItr->second.incoming.empty() )
		{
			auto& simulationActions = simulationItr->second.actions;
			for( auto simulationAction : simulationActions )
			{
				auto actionName = simulationAction->name();
				actionNames.push_back( actionName );
			}
			simulationItr = simulations.find( *simulationItr->second.incoming.begin() );
		}

		if( actionNames.empty() )
		{
			continue;
		}

		auto actionNameItr = actionNames.rbegin();
		std::cout << *actionNameItr;
		actionNameItr++;

		while( actionNameItr != actionNames.rend() )
		{
			std::cout << " | " << *actionNameItr;
			actionNameItr++;
		}

		std::cout << std::endl << "==============" << std::endl;
	}
}

void printPlannedActions(
	const std::vector< std::shared_ptr< gie::Action > >& plannedActions,
	gie::StringRegister& stringRegister )
{
	for( auto action : plannedActions )
	{
		if( action )
		{
			auto registeredString = stringRegister.get( action->hash() );
			if( !registeredString.empty() )
			{
				std::cout << registeredString << std::endl;
			}
			else
			{
				std::cout << action->name() << std::endl;
			}
		}
	}
}