#include "goapie.h"

int insertingDataEntities()
{
	gie::World world;
	gie::Guid lastEntityGuid{ gie::NullGuid };

	for( size_t i = 0; i < 10; i++ )
	{
		lastEntityGuid = world.createEntity().first;
	}
	
	auto entity = world.entity( lastEntityGuid );

	return 0;
}

int insertingParameters()
{
	gie::World world;
	auto entityGuid = world.createEntity().first;
	auto entity = world.entity( entityGuid );

	if( !entity )
	{
		return 1;
	}

	auto [ param1Guid, param1Ptr ] = entity->createProperty( "AmmoCount" );

	if( param1Ptr )
	{
		param1Ptr->value = 5;
	}

	auto [ param2Guid, param2Ptr ] = entity->createProperty( "Name" );

	if( param2Ptr )
	{
		param2Ptr->value = gie::stringHasher( "BFG" );
	}

	return 0;
}

void printPlannedActions( const std::vector< std::shared_ptr< gie::Action > >& plannedActions, gie::StringRegister& stringRegister )
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

int basicGoal()
{
	// creating world
	gie::World world;

	// adding door entity to world
	auto [ doorEntityGuid, doorEntity ] = world.createEntity();

	// adding property to door entity and setting its default value
	auto [ doorOpenedPptGuid, doorOpenedPpt ] = doorEntity->createProperty( "Opened", false );

	// creating goal
	gie::Goal goal{ world };

	// setting goal targets (door must get opened)
	goal.targets.emplace_back( doorOpenedPptGuid, true );

	// creating agent (aka npc)
	auto [ agentEntityGuid, agentEntity ] = world.createAgent();

	// agent holds a property used by open door simulator
	agentEntity->createProperty( "TargetDoorEntity", doorEntityGuid );

	// setting agent and door locations so there will be a Move action before OpenDoor
	// NOTE: these can be commented out and plan will only contain an OpenDoor action
	agentEntity->createProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
	doorEntity->createProperty( "Location", glm::vec3{ 0.f, 0.f, 1.f } );

	// creating string register to point string hashes back to strings
	static gie::StringRegister stringRegister;

	// defining a move action to be used by open door action
	class MoveAction : public gie::Action
	{
	public:

		// inheriting constructors
		using gie::Action::Action;

		std::string_view name() const override { return "Move"; }
		gie::StringHash hash() const override { return stringRegister.add( "Move" ); }

		// defines how world context and agent are affected by this action
		bool outcome( gie::Agent& agent ) override
		{
			// geting target location
			gie::Guid targetLocationPptGuid = std::get< gie::Guid >( arguments().get( "TargetLocation" ) );
			auto locationPpt = agent.world()->property( targetLocationPptGuid );
			if( !locationPpt )
			{
				return false;
			}

			auto location = locationPpt->getVec3();
			if( !location.first )
			{
				return false;
			}

			// updating agent world location
			auto agentLocation = agent.property( "Location" );
			if( !agentLocation.second )
			{
				return false;
			}
			
			agentLocation.second->value = location.second;

			return true;
		}
	};

	// defining action to open door
	class OpenDoorAction : public gie::Action
	{
	public:
		
		// inheriting constructors
		using gie::Action::Action;

		gie::StringHash hash() const override { return stringRegister.add( "OpenDoor" ); }

		// defines how world context and agent are affected by this action
		bool outcome( gie::Agent& agent ) override
		{
			// updating agent opinion over world
			gie::Guid openedPptGuid = std::get< gie::Guid >( arguments().get( "Opened" ) );
			auto localPpt = agent.opinions().property( openedPptGuid );
			if( localPpt )
			{
				localPpt->value = true;
			}

			// updating world property
			auto globalPpt = agent.world()->property( openedPptGuid );
			if( globalPpt )
			{
				globalPpt->value = true;
			}

			// all went good, outcome set properly
			bool allGood = localPpt && globalPpt;

			return allGood;
		}
	};

	// defining simulator for action open door
	class OpenDoorSimulator : public gie::ActionSimulator
	{
	public:
		
		using gie::ActionSimulator::ActionSimulator;

		std::string_view name() const override { return "OpenDoor"; }
		gie::StringHash hash() const override { return stringRegister.add( "OpenDoor" ); }

		// define conditions for action
		bool prerequisites( const gie::Simulation& simulation, const gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			// checking if world context agent has property telling which door entity is the target
			auto [ targetDoorEntityPptGuid, targetDoorEntityPpt ] = agent.worldContextAgent()->property( "TargetDoorEntity" );
			if( !targetDoorEntityPpt )
			{
				return false;
			}

			// checking if there is an actual door entity in the world
			auto doorEntity = simulation.world()->entity( targetDoorEntityPpt->getGuid().second );
			if( !doorEntity )
			{
				return false;
			}

			auto openedPpt = doorEntity->property( "Opened" );
			// not valid if no property "Opened" exists
			if( !openedPpt.second )
			{
				return false;
			}

			// not valid if door is already opened
			if( openedPpt.second->getBool().second == true )
			{
				return false;
			}

			return true;
		}

		// calculate cost and necessary steps (other actions) to achieve the action being simulated
		bool simulate( gie::Simulation& simulation, gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			// setting base cost as starting point
			constexpr float baseCost = 10.f;
			simulation.cost = baseCost;

			// checking if world context agent has property telling which door entity is the target
			auto [ targetDoorEntityPptGuid, targetDoorEntityPpt ] = agent.worldContextAgent()->property( "TargetDoorEntity" );
			if( !targetDoorEntityPpt )
			{
				return false;
			}

			// checking if there is an actual door entity in the world
			auto doorEntity = simulation.world()->entity( targetDoorEntityPpt->getGuid().second );
			if( !doorEntity )
			{
				return false;
			}

			// setting simulation property
			auto [ openedPptGuid, openedPpt ] = doorEntity->property( "Opened" );
			if( !openedPpt )
			{
				return false;
			}

			simulation.context().property( openedPpt->guid() )->value = true;
			
			// adding distance cost in case there are location properties
			auto [ doorLocationPptGuid, doorLocationPpt ] = doorEntity->property( "Location" );
			auto [ agentLocationPptGuid, agentLocationPpt ] = agent.worldContextAgent()->property( "Location" );
			if( doorLocationPpt && agentLocationPpt )
			{
				const glm::vec3 doorLocation = std::get< glm::vec3 >( doorLocationPpt->value );
				const glm::vec3 agentLocation = std::get< glm::vec3 >( agentLocationPpt->value );
				const float dist = glm::distance( doorLocation, agentLocation );
				simulation.cost += dist;

				// creating move action as door is far from agent
				if( auto moveAction = std::make_shared< MoveAction >() )
				{
					// passing target location as argument for action
					moveAction->arguments().add( { gie::stringHasher( "TargetLocation" ), doorLocationPptGuid } );
					// queueing move action
					simulation.actions.emplace_back( moveAction );
				}
			}

			// creating open door action
			if( auto openDoorAction = std::make_shared< OpenDoorAction >( arguments() ) )
			{
				// passing door entity as argument for action
				openDoorAction->arguments().add( { gie::stringHasher( "DoorEntity" ), doorEntity->guid() } );
				// queueing open door action
				simulation.actions.emplace_back( openDoorAction );
				return true;
			}

			return false;
		}

	};

	// creating planner passing goal and agent to reach the goal
	gie::Planner planner{ goal, *agentEntity };

	// defining available action and its simulator for planner
	DEFINE_ACTION_SET_ENTRY( OpenDoor )

	// setting available actions
	planner.addActionSetEntry< OpenDoorActionSetEntry >( gie::stringHasher( "OpenDoor" ) );

	// finally planner doing its thing
	planner.plan();

	// printing planned actions
	printPlannedActions( planner.planActions(), stringRegister );

	return 0;
}

int woodHouse()
{
	// creating world
	gie::World world;

	// creating agent (aka npc)
	auto [ agentEntityGuid, agentEntity ] = world.createAgent();

	// property telling if agent has a wood house
	 auto [ agentWoodHousePptGuid, agentWoodHousePpt ] = agentEntity->createProperty( "WoodHouse", false );

	// creating agent properties for this tutorial
	agentEntity->createProperty( "Money", 0.f );
	agentEntity->createProperty( "MoneyNeeded", 0.f );
	agentEntity->createProperty( "AxeIntegrity", 0.f );

	// cost of buying an axe
	constexpr float axeCost = 15.f;
	// money earn from work
	constexpr float workSalary = 20.0;
	// brand new axe integrity 
	static constexpr float newAxeIntegrityValue = 3.f;

	// creating goal
	gie::Goal goal{ world };

	// setting goal targets (agent's wood house must exist)
	goal.targets.emplace_back( agentWoodHousePptGuid, true );

	// creating string register to point string hashes back to strings
	static gie::StringRegister stringRegister;

	// defining a cut down tree action aiming a target tree
	class CutDownTreeAction : public gie::Action
	{
	public:

		// inheriting constructors
		using gie::Action::Action;

		std::string_view name() const override { return "CutDownTree"; }
		gie::StringHash hash() const override { return stringRegister.add( "CutDownTree" ); }

		// defines how world context and agent are affected by this action
		bool outcome( gie::Agent& agent ) override
		{
			// getting target tree from arguments
			gie::Guid targetTreeGuid = std::get< gie::Guid >( arguments().get( "TargetTree" ) );
			auto targetTree = agent.world()->entity( targetTreeGuid );
			if( !targetTree )
			{
				return false;
			}

			// retagging tree
			auto& entityTagRegister = agent.world()->context().entityTagRegister();
			entityTagRegister.untag( targetTree, { gie::stringHasher( "TreeUp" ) } );
			entityTagRegister.tag( targetTree, { gie::stringHasher( "TreeDown" ) } );

			return true;
		}
	};

	// minimal amount of integrity an axe need to have to cut down a tree, so there is no need to buy another one
	constexpr float minIntegrity = 1.f;

	// defining simulator for action cut down tree
	class CutDownTreeSimulator : public gie::ActionSimulator
	{
	public:
		
		using gie::ActionSimulator::ActionSimulator;

		std::string_view name() const override { return "CutDownTree"; }
		gie::StringHash hash() const override { return stringRegister.add( "CutDownTree" ); }

		// define conditions for action
		bool prerequisites( const gie::Simulation& baseSimulation, const gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			// getting Axe Integrity property guid from world context
			auto [ axeIntegrityPptGuid, _ ] = agent.worldContextAgent()->property( "AxeIntegrity" );

			// getting Axe Integrity property from current simulation context
			auto simAxeIntegrityPpt = baseSimulation.context().property( axeIntegrityPptGuid );

			// return if no property was found
			if( !simAxeIntegrityPpt )
			{
				return false;
			}

			// checking minimal axe integrity to cut down a tree
			if( simAxeIntegrityPpt->getFloat().second <= minIntegrity )
			{
				return false;
			}

			// getting set of trees still up
			const auto treeUpTagSet = baseSimulation.tagSet( gie::stringHasher( "TreeUp" ) );

			// no tag set found
			if( !treeUpTagSet )
			{
				return false;
			}

			// no tree up found
			if( treeUpTagSet->empty() )
			{
				return false;
			}

			return true;
		}

		// calculate cost and necessary steps (other actions) to achieve the action being simulated
		bool simulate( gie::Simulation& simulation, gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			// getting Axe Integrity property guid from world context
			auto [ axeIntegrityPptGuid, _ ] = agent.worldContextAgent()->property( "AxeIntegrity" );

			// getting Axe Integrity property from current simulation context
			auto simAxeIntegrityPpt = simulation.context().property( axeIntegrityPptGuid );

			// decreasing axe integrity once tree was cut down
			simAxeIntegrityPpt->value = simAxeIntegrityPpt->getFloat().second - 1.f;

			// getting set of trees still up
			const auto treeUpTagSet = simulation.tagSet( gie::stringHasher( "TreeUp" ) );

			// consuming first tree
			gie::Guid treeEntityGuid = *treeUpTagSet->cbegin();
			auto& simEntityTagRegister = simulation.context().entityTagRegister();
			simEntityTagRegister.untag( treeEntityGuid, { gie::stringHasher( "TreeUp" ) } );
			simEntityTagRegister.tag( treeEntityGuid, { gie::stringHasher( "TreeDown" ) } );

			// creating cut down tree action
			if( auto cutDownTreeAction = std::make_shared< CutDownTreeAction >( arguments() ) )
			{
				// passing target tree entity guid as argument for action
				cutDownTreeAction->arguments().add( { gie::stringHasher( "TargetTree" ), *treeUpTagSet->cbegin() } );
				// queueing action
				simulation.actions.emplace_back( cutDownTreeAction );
				return true;
			}

			return false;
		}

	};

	// defining a cut down tree action aiming a target tree
	class WorkAction : public gie::Action
	{
	public:

		// inheriting constructors
		using gie::Action::Action;

		std::string_view name() const override { return "Work"; }
		gie::StringHash hash() const override { return stringRegister.add( "Work" ); }

		// defines how world context and agent are affected by this action
		bool outcome( gie::Agent& agent ) override
		{
			auto [ _, moneyPpt ] = agent.property( "Money" );
			auto [ __, moneyNeededPpt ] = agent.property( "MoneyNeeded" );

			// adding money to agent in world's context
			moneyPpt->value = moneyPpt->getFloat().second + workSalary;

			// updating money needed
			moneyNeededPpt->value = std::get< float >( arguments().get( "NewMoneyNeeded" ) );

			return true;
		}
	};

	// defining simulator for action cut down tree
	class WorkSimulator : public gie::ActionSimulator
	{
	public:
		
		using gie::ActionSimulator::ActionSimulator;

		std::string_view name() const override { return "Work"; }
		gie::StringHash hash() const override { return stringRegister.add( "Work" ); }

		// define conditions for action
		bool prerequisites( const gie::Simulation& baseSimulation, const gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			// getting property Guid to refer in the simulation
			auto [ moneyPptGuid, _ ] = agent.worldContextAgent()->property( "Money" );
			auto [ moneyNeededPptGuid, __ ] = agent.worldContextAgent()->property( "MoneyNeeded" );

			// getting property in simulation property
			const auto simMoneyPpt = baseSimulation.context().property( moneyPptGuid );
			const auto simMoneyNeededPpt = baseSimulation.context().property( moneyNeededPptGuid );

			// no need to work if has all money needed
			if( simMoneyPpt->getFloat().second >= simMoneyNeededPpt->getFloat().second )
			{
				return false;
			}

			return true;
		}

		// calculate cost and necessary steps (other actions) to achieve the action being simulated
		bool simulate( gie::Simulation& simulation, gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			// getting property Guid to refer in the simulation
			auto [ moneyPptGuid, _ ] = agent.worldContextAgent()->property( "Money" );
			auto [ moneyNeededPptGuid, __ ] = agent.worldContextAgent()->property( "MoneyNeeded" );

			// getting property in simulation property
			auto simMoneyPpt = simulation.context().property( moneyPptGuid );
			const auto simMoneyNeededPpt = simulation.context().property( moneyNeededPptGuid );

			// setting money property in simulation's context
			simMoneyPpt->value = simMoneyPpt->getFloat().second + workSalary;

			// remove the need for money if all money needed was raised
			if( simMoneyPpt->getFloat().second >= simMoneyNeededPpt->getFloat().second )
			{
				simMoneyNeededPpt->value = 0.f;
			}

			// creating arguments for action
			gie::NamedArguments actionArguments{};
			actionArguments.add( { gie::stringHasher( "NewMoneyNeeded" ), simMoneyNeededPpt->getFloat().second } );

			// creating work action
			if( auto workAction = std::make_shared< WorkAction >( actionArguments ) )
			{
				// queueing action
				simulation.actions.emplace_back( workAction );
				return true;
			}

			return false;
		}

	};

	// defining a raise money needed action so npc need to find an way to get more money
	class RaiseMoneyNeededAction : public gie::Action
	{
	public:

		// inheriting constructors
		using gie::Action::Action;

		std::string_view name() const override { return "MoneyNeeded"; }
		gie::StringHash hash() const override { return stringRegister.add( "MoneyNeeded" ); }

		// defines how world context and agent are affected by this action
		bool outcome( gie::Agent& agent ) override
		{
			// getting agent property in world context
			auto [ _, MoneyNeededPpt ] = agent.property( "MoneyNeeded" );
			if( !MoneyNeededPpt )
			{
				return false;
			}

			// setting axe integrity to agent in world context
			MoneyNeededPpt->value = MoneyNeededPpt->getFloat().second + std::get< float >( arguments().get( gie::stringHasher( "MoreMoneyNeeded" ) ) );

			return true;
		}
	};


	// defining a buy axe action in case npc needs it
	class BuyAxeAction : public gie::Action
	{
	public:

		// inheriting constructors
		using gie::Action::Action;

		std::string_view name() const override { return "BuyAxe"; }
		gie::StringHash hash() const override { return stringRegister.add( "BuyAxe" ); }

		// defines how world context and agent are affected by this action
		bool outcome( gie::Agent& agent ) override
		{
			// getting agent axe integrity property in world context
			auto [ _, axeIntegrityPpt ] = agent.property( "AxeIntegrity" );
			if( !axeIntegrityPpt )
			{
				return false;
			}

			// setting axe integrity to agent in world context
			axeIntegrityPpt->value = newAxeIntegrityValue;

			// getting agent money property in world context
			auto [ __, moneyPpt ] = agent.property( "Money" );
			if( !moneyPpt )
			{
				return false;
			}

			// setting new money value to agent in world context
			moneyPpt->value = std::max( moneyPpt->getFloat().second - axeCost, 0.f );

			return true;
		}
	};

	// defining simulator for action buy axe
	class BuyAxeSimulator : public gie::ActionSimulator
	{
	public:
		
		using gie::ActionSimulator::ActionSimulator;

		std::string_view name() const override { return "BuyAxe"; }
		gie::StringHash hash() const override { return stringRegister.add( "BuyAxe" ); }

		// define conditions for action
		bool prerequisites( const gie::Simulation& baseSimulation, const gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			// getting agent axe integrity property guid from world context
			auto [ axeIntegrityPptGuid, _ ] = agent.worldContextAgent()->property( "AxeIntegrity" );

			// getting axe integrity property from simulation context
			auto simAxeIntegrityPpt = baseSimulation.context().property( axeIntegrityPptGuid );
			if( !simAxeIntegrityPpt )
			{
				return false;
			}

			// checking minimal axe integrity
			if( simAxeIntegrityPpt->getFloat().second >= minIntegrity )
			{
				return false;
			}

			// getting agent money property guid from world context
			auto [ moneyPptGuid, __ ] = agent.worldContextAgent()->property( "Money" );

			// getting money property from simulation context
			auto simMoneyPpt = baseSimulation.context().property( moneyPptGuid );
			if( !simMoneyPpt )
			{
				return false;
			}

			// checking if has enough money to buy axe
			if( simMoneyPpt->getFloat().second < axeCost )
			{
				// it will raise money needed later in simulation
				// so no axe is going to bought but it will queue
				// a RaiseMoneyNeeded action so npc need to get
				// more money so it can actually afford an axe.
				return true;
			}

			return true;
		}

		// calculate cost and necessary steps (other actions) to achieve the action being simulated
		bool simulate( gie::Simulation& simulation, gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			// getting agent money property guid from world context
			auto [ moneyPptGuid, _ ] = agent.worldContextAgent()->property( "Money" );

			// getting money property from simulation context
			auto simMoneyPpt = simulation.context().property( moneyPptGuid );
			if( !simMoneyPpt )
			{
				return false;
			}

			// checking if has enough money to buy axe
			if( simMoneyPpt->getFloat().second >= axeCost )
			{
				// getting agent axe integrity property guid from world context
				auto [ axeIntegrityPptGuid, _ ] = agent.worldContextAgent()->property( "AxeIntegrity" );

				// getting axe integrity property from simulation context
				auto simAxeIntegrityPpt = simulation.context().property( axeIntegrityPptGuid );
				if( !simAxeIntegrityPpt )
				{
					return false;
				}

				// setting new axe integrity value
				simAxeIntegrityPpt->value = newAxeIntegrityValue;

				// creating buy axe action
				if( auto buyAxeAction = std::make_shared< BuyAxeAction >( arguments() ) )
				{
					// queueing action
					simulation.actions.emplace_back( buyAxeAction );
					return true;
				}
			}
			else
			{
				// no money to buy an axe, lets raise money needed so
				// agent (character in simulation) and npc (actual character in game)
				// will look for more money.

				// getting agent money property guid from world context
				auto [ moneyNeededPptGuid, __ ] = agent.worldContextAgent()->property( "MoneyNeeded" );

				// getting money property from simulation context
				auto simMoneyNeededPpt = simulation.context().property( moneyNeededPptGuid );
				if( !simMoneyNeededPpt )
				{
					return false;
				}

				// raising money needed in simulation context
				simMoneyNeededPpt->value = simMoneyNeededPpt->getFloat().second + axeCost;

				// creating arguments for action
				gie::NamedArguments actionArguments{};
				actionArguments.add( { gie::stringHasher( "MoreMoneyNeeded" ), simMoneyNeededPpt->getFloat().second } );

				// creating raise money needed action
				if( auto raiseMoneyNeededAction = std::make_shared< RaiseMoneyNeededAction >( actionArguments ) )
				{
					// queueing action
					simulation.actions.emplace_back( raiseMoneyNeededAction );
					return true;
				}
			}

			return true;
		}

	};

	// adding trees to world
	constexpr size_t treeCount = 6;
	for( size_t i = 0; i < treeCount; i++ )
	{
		auto [ treeEntityGuid, treeEntity ] = world.createEntity();
		world.context().entityTagRegister().tag( treeEntity, { gie::stringHasher( "Tree" ), gie::stringHasher( "TreeUp" ) } );
	}

	// creating planner passing goal and agent to reach the goal
	gie::Planner planner{ goal, *agentEntity };

	// defining available actions and their simulators for planner
	DEFINE_ACTION_SET_ENTRY( CutDownTree )
	DEFINE_ACTION_SET_ENTRY( Work )
	DEFINE_ACTION_SET_ENTRY( BuyAxe )

	// setting available actions
	planner.addActionSetEntry< CutDownTreeActionSetEntry >( gie::stringHasher( "CutDownTree" ) );
	planner.addActionSetEntry< WorkActionSetEntry >( gie::stringHasher( "Work" ) );
	planner.addActionSetEntry< BuyAxeActionSetEntry >( gie::stringHasher( "BuyAxe" ) );

	// finally planner doing its thing
	planner.plan();

	// printing actions from simulation leaf nodes
	std::vector< gie::Guid > leafSimulationGuids{ };
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

	// printing planned actions
	printPlannedActions( planner.planActions(), stringRegister );

	return 0;
}

int main( int argc, char** argv )
{
	return woodHouse();
}