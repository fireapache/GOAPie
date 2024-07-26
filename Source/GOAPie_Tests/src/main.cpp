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

int basicGoal()
{
	// creating world
	gie::World world;

	// adding entity to world
	auto [ entityGuid, entityPtr ] = world.createEntity();

	// adding property to entity and setting it's default value
	auto [ openedGuid, openedPtr ] = entityPtr->createProperty( "Opened", false );

	// Defining a move action as well
	class MoveAction : public gie::Action
	{
	public:

		// inheriting constructors
		using gie::Action::Action;

		gie::StringHash name() const override { return gie::stringHasher( "Move" ); }

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

	// defining action for opening door
	class OpenDoorAction : public gie::Action
	{
	public:
		
		// inheriting constructors
		using gie::Action::Action;

		gie::StringHash name() const override { return gie::stringHasher( "OpenDoor" ); }

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

		gie::StringHash name() const override { return gie::stringHasher( "OpenDoor" ); }

		// define conditions for action
		bool prerequisites( const gie::Simulation& context, const gie::Agent& agent ) const override
		{
			gie::Guid doorEntityGuid = arguments().guid( "DoorEntity" );
			auto entityPtr = agent.world()->entity( doorEntityGuid ); 
			// not valid if no target door entity is found
			if( !entityPtr )
			{
				return false;
			}

			auto openedPpt = entityPtr->property( "Opened" );
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

			gie::Guid doorEntityGuid = arguments().guid( "DoorEntity" );
			auto entityPtr = agent.world()->entity( doorEntityGuid );
			// not valid if no target door entity is found
			if( !entityPtr )
			{
				return false;
			}

			// adding distance cost in case there are location properties
			auto doorLocationPpt = entityPtr->property( "Location" );
			auto agentLocationPpt = agent.property( "Location" );
			if( doorLocationPpt.second && agentLocationPpt.second )
			{
				const glm::vec3 doorLocation = std::get< glm::vec3 >( doorLocationPpt.second->value );
				const glm::vec3 agentLocation = std::get< glm::vec3 >( agentLocationPpt.second->value );
				const float dist = glm::distance( doorLocation, agentLocation );
				simulation.cost += dist;

				// adding move action as door is far from agent
				if( auto moveAction = std::make_shared< MoveAction >( arguments() ) )
				{
					simulation.actions.emplace_back( moveAction );
				}
			}

			// adding open door action
			if( auto openDoorAction = std::make_shared< OpenDoorAction >( arguments() ) )
			{
				simulation.actions.emplace_back( openDoorAction );
			}
			
			return true;
		}

	};

	// creating goal
	gie::Goal goal{ world };

	// setting goal targets (door must get opened)
	goal.targets.emplace_back( openedGuid, true );

	// creating agent
	auto [ agentGuid, agentPtr ] = world.createAgent();

	// creating planner
	gie::Planner planner{ goal, *agentPtr };

	// defining available action and its simulator for planner
	DEFINE_ACTION_SET_ENTRY( OpenDoor )

	// setting available actions;
	planner.actionSet().emplace( gie::stringHasher( "OpenDoor" ), OpenDoorActionSetEntry() );

	return 0;
}

int main( int argc, char** argv )
{
	return basicGoal();
}