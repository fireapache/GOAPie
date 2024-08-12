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
			auto registeredString = stringRegister.get( action->name() );
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

	// Defining a move action to be used by open door action
	class MoveAction : public gie::Action
	{
	public:

		// inheriting constructors
		using gie::Action::Action;

		gie::StringHash name() const override { return stringRegister.add( "Move" ); }

		// defines how world and agent are affected by this action
		bool outcome( gie::Agent& agent ) override
		{
			// geting target location
			gie::Guid targetLocationPptGuid = arguments().guid( "TargetLocation" );
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

		gie::StringHash name() const override { return stringRegister.add( "OpenDoor" ); }

		// defines how world and agent are affected by this action
		bool outcome( gie::Agent& agent ) override
		{
			// updating agent opinion over world
			gie::Guid openedPptGuid = arguments().guid( "Opened" );
			auto localPpt = agent.opinions.property( openedPptGuid );
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

		gie::StringHash name() const override { return stringRegister.add( "OpenDoor" ); }

		// define conditions for action
		bool prerequisites( const gie::Simulation& context, const gie::Agent& agent ) const override
		{
			// checking if agent has property telling which door entity is the target
			auto targetDoorEntityPpt = agent.property( "TargetDoorEntity" ).second;
			if( !targetDoorEntityPpt )
			{
				return false;
			}

			// checking if there is an actual door entity in the world
			auto doorEntity = agent.world()->entity( targetDoorEntityPpt->getGuid().second );
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
		bool simulate( gie::Agent& agent, gie::Simulation& simulation ) const override
		{
			// setting base cost as starting point
			constexpr float baseCost = 10.f;
			simulation.cost = baseCost;

			// checking if agent has property telling which door entity is the target
			auto targetDoorEntityPpt = agent.property( "TargetDoorEntity" ).second;
			if( !targetDoorEntityPpt )
			{
				return false;
			}

			// checking if there is an actual door entity in the world
			auto doorEntity = agent.world()->entity( targetDoorEntityPpt->getGuid().second );
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

			simulation.setProperty( openedPpt->guid(), true );
			
			// adding distance cost in case there are location properties
			auto doorLocationPpt = doorEntity->property( "Location" );
			auto agentLocationPpt = agent.property( "Location" );
			if( doorLocationPpt.second && agentLocationPpt.second )
			{
				const glm::vec3 doorLocation = std::get< glm::vec3 >( doorLocationPpt.second->value );
				const glm::vec3 agentLocation = std::get< glm::vec3 >( agentLocationPpt.second->value );
				const float dist = glm::distance( doorLocation, agentLocation );
				simulation.cost += dist;

				// creating move action as door is far from agent
				if( auto moveAction = std::make_shared< MoveAction >() )
				{
					// passing target location as argument for action
					moveAction->arguments().add( { gie::stringHasher( "TargetLocation" ), doorLocationPpt.first } );
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
			}

			return true;
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

	// creating goal
	gie::Goal goal{ world };

	// setting goal targets (agent's wood house must exist)
	goal.targets.emplace_back( agentWoodHousePptGuid, true );

	// creating string register to point string hashes back to strings
	static gie::StringRegister stringRegister;

	// Defining a cut down tree action aiming a target tree
	class CutDownTreeAction : public gie::Action
	{
	public:

		// inheriting constructors
		using gie::Action::Action;

		gie::StringHash name() const override { return stringRegister.add( "CutDownTree" ); }

		// defines how world and agent are affected by this action
		bool outcome( gie::Agent& agent ) override
		{
			// getting target tree from arguments
			gie::Guid targetTreeGuid = arguments().guid( "TargetTree" );
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

	// defining simulator for action cut down tree
	class CutDownTreeSimulator : public gie::ActionSimulator
	{
	public:
		
		using gie::ActionSimulator::ActionSimulator;

		gie::StringHash name() const override { return stringRegister.add( "CutDownTree" ); }

		// define conditions for action
		bool prerequisites( const gie::Simulation& baseSimulation, const gie::Agent& agent ) const override
		{
			auto& entityTagRegister = baseSimulation.context().entityTagRegister();

			// getting set of trees still up
			auto treeUpTagSet = entityTagRegister.tagSet( gie::stringHasher( "TreeUp" ) );

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
		bool simulate( gie::Agent& agent, gie::Simulation& simulation ) const override
		{
			auto& entityTagRegister = simulation.context().entityTagRegister();

			// getting set of trees still up
			auto treeUpTagSet = entityTagRegister.tagSet( gie::stringHasher( "TreeUp" ) );

			// creating cut down tree action
			if( auto cutDownTreeAction = std::make_shared< CutDownTreeAction >( arguments() ) )
			{
				// passing target tree entity guid as argument for action
				cutDownTreeAction->arguments().add( { gie::stringHasher( "TargetTree" ), *treeUpTagSet->cbegin() } );
				// queueing action
				simulation.actions.emplace_back( cutDownTreeAction );
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

	// defining available action and its simulator for planner
	DEFINE_ACTION_SET_ENTRY( CutDownTree )

	// setting available actions
	planner.addActionSetEntry< CutDownTreeActionSetEntry >( gie::stringHasher( "CutDownTree" ) );

	// finally planner doing its thing
	planner.plan();

	// printing planned actions
	printPlannedActions( planner.planActions(), stringRegister );

	return 0;
}

int main( int argc, char** argv )
{
	return woodHouse();
}