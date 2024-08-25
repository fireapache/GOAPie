#include "goapie.h"

void printPlannedActions( const std::vector< std::shared_ptr< gie::Action > >& plannedActions, gie::StringRegister& stringRegister );

int openDoor()
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