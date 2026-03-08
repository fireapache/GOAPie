#include <goapie.h>

#include "example.h"

const char* openDoorDescription()
{
    return "Simple example demonstrating opening a door via planner actions.";
}

extern void printPlannedActions( const std::vector< std::shared_ptr< gie::Action > >& plannedActions );

int openDoor( ExampleParameters& params )
{
	gie::World& world = params.world;
	gie::Planner& planner = params.planner;
	gie::Goal& goal = params.goal;

	// adding door entity to world
	auto doorEntity = world.createEntity();

	// adding property to door entity and setting its default value
	auto doorOpenedPpt = doorEntity->createProperty( "Opened", false );

	// setting goal targets (door must get opened)
	goal.targets.emplace_back( doorOpenedPpt->guid(), true );

	// creating agent (aka npc)
	auto agentEntity = world.createAgent();

	// agent holds a property used by open door simulator
	agentEntity->createProperty( "TargetDoorEntity", doorEntity->guid() );

	// setting agent and door locations so there will be a Move action before OpenDoor
	// NOTE: these can be commented out and plan will only contain an OpenDoor action
	agentEntity->createProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
	doorEntity->createProperty( "Location", glm::vec3{ 0.f, 0.f, 1.f } );

	// setting up planner passing goal and agent to reach the goal
	planner.simulate( goal, *agentEntity );

	// defining available action simulator for OpenDoor
	planner.addLambdaAction( "OpenDoor",
		// define conditions for action
		[]( gie::EvaluateSimulationParams params ) -> bool
		{
			// checking if world context agent has property telling which door entity is the target
			auto targetDoorEntityPpt = params.agent.worldContextAgent()->property( "TargetDoorEntity" );
			if( !targetDoorEntityPpt )
			{
				return false;
			}

			// checking if there is an actual door entity in the world
			auto doorEntity = params.simulation.world()->entity( *targetDoorEntityPpt->getGuid() );
			if( !doorEntity )
			{
				return false;
			}

			auto openedPpt = doorEntity->property( "Opened" );
			// not valid if no property "Opened" exists
			if( !openedPpt )
			{
				return false;
			}

			// not valid if door is already opened
			if( *openedPpt->getBool() == true )
			{
				return false;
			}

			return true;
		},
		// calculate cost and necessary steps (other actions) to achieve the action being simulated
		[]( gie::SimulateSimulationParams params ) -> bool
		{
			// setting base cost as starting point
			constexpr float baseCost = 10.f;
			params.simulation.cost = baseCost;

			// checking if world context agent has property telling which door entity is the target
			auto targetDoorEntityPpt = params.agent.worldContextAgent()->property( "TargetDoorEntity" );
			if( !targetDoorEntityPpt )
			{
				return false;
			}

			// checking if there is an actual door entity in the world
			auto doorEntity = params.simulation.world()->entity( *targetDoorEntityPpt->getGuid() );
			if( !doorEntity )
			{
				return false;
			}

			// setting simulation property
			auto openedPpt = doorEntity->property( "Opened" );
			if( !openedPpt )
			{
				return false;
			}

			params.simulation.context().property( openedPpt->guid() )->value = true;

			// adding distance cost in case there are location properties
			auto doorLocationPpt = doorEntity->property( "Location" );
			auto agentLocationPpt = params.agent.worldContextAgent()->property( "Location" );
			if( doorLocationPpt && agentLocationPpt )
			{
				const glm::vec3 doorLocation = std::get< glm::vec3 >( doorLocationPpt->value );
				const glm::vec3 agentLocation = std::get< glm::vec3 >( agentLocationPpt->value );
				const float dist = glm::distance( doorLocation, agentLocation );
				params.simulation.cost += dist;

				// creating move action as door is far from agent
				auto moveAction = std::make_shared< gie::Action >( gie::stringHasher( "Move" ) );
				// passing target location as argument for action
				moveAction->arguments().add( { gie::stringHasher( "TargetLocation" ), doorLocationPpt->guid() } );
				// queueing move action
				params.simulation.actions.emplace_back( moveAction );
			}

			// creating open door action
			auto openDoorAction = std::make_shared< gie::Action >( gie::stringHasher( "OpenDoor" ) );
			// passing door entity as argument for action
			openDoorAction->arguments().add( { gie::stringHasher( "DoorEntity" ), doorEntity->guid() } );
			// queueing open door action
			params.simulation.actions.emplace_back( openDoorAction );
			return true;
		}
	);

	// finally planner doing its thing
	planner.plan();

	// printing planned actions
	printPlannedActions( planner.planActions() );

	return 0;
}

int openDoorValidateResult( std::string& failMsg )
{
	gie::World world{};
	gie::Planner planner{};
	gie::Goal goal{ world };
	ExampleParameters params{ world, planner, goal };

	VALIDATE( openDoor( params ) == 0, "openDoor() setup failed" );

	auto& planned = planner.planActions();
	VALIDATE_EQ( planned.size(), size_t( 2 ), "planned action count" );

	// Verify action names: Move then OpenDoor (backtrack order from single simulation node)
	VALIDATE_STR_EQ( planned[ 0 ]->name(), "Move", "planned action[0]" );
	VALIDATE_STR_EQ( planned[ 1 ]->name(), "OpenDoor", "planned action[1]" );

	VALIDATE_EQ( planner.simulations().size(), size_t( 2 ), "simulation count" );

	return 0;
}
