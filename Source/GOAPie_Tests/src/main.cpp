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

	// defining simulation for action open door
	class OpenDoorSimulation : public gie::ActionSimulation
	{
		gie::Guid _doorEntityGuid{ gie::NullGuid };

	public:
		OpenDoorSimulation() = delete;
		OpenDoorSimulation( gie::Planner& planner, gie::Guid doorEntityGuid ) : ActionSimulation( planner ), _doorEntityGuid( doorEntityGuid ) { }
		OpenDoorSimulation( OpenDoorSimulation&& ) = default;
		~OpenDoorSimulation() = default;

		gie::StringHash actionName() const override { return gie::stringHasher( "Open Door" ); }

		bool prerequisites( const gie::Simulation& context, const gie::Agent& agent ) const override
		{
			auto entityPtr = agent.world()->entity( _doorEntityGuid ); 
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

		bool simulate( const gie::Simulation& base ) const override
		{
			auto [ simGuid, simResult ] = planner()->createSimulation( base.guid() );

			// setting base cost as starting point
			constexpr float baseCost = 10.f;
			simResult->cost = baseCost;
			
			auto agent = planner()->agent();

			auto entityPtr = agent->world()->entity( _doorEntityGuid );
			// not valid if no target door entity is found
			if( !entityPtr )
			{
				return false;
			}

			// adding distance cost in case there are location properties
			auto doorLocationPpt = entityPtr->property( "Location" );
			auto agentLocationPpt = agent->property( "Location" );
			if( doorLocationPpt.second && agentLocationPpt.second )
			{
				const glm::vec3 doorLocation = std::get< glm::vec3 >( doorLocationPpt.second->value );
				const glm::vec3 agentLocation = std::get< glm::vec3 >( agentLocationPpt.second->value );
				const float dist = glm::distance( doorLocation, agentLocation );
				simResult->cost += dist;
			}

			//simResult->actions.emplace_back();
		}

	};

	// defining action for opening door
	class OpenDoorAction : public gie::Action
	{
		gie::Guid _doorEntityGuid{ gie::NullGuid };

	public:
		OpenDoorAction() = delete;
		OpenDoorAction( gie::Guid doorEntityGuid ) : _doorEntityGuid( doorEntityGuid ) { }
		~OpenDoorAction() = default;

		gie::StringHash actionName() const { return gie::stringHasher( "Open Door" ); }

		bool outcome( gie::Agent& agent ) override
		{
			// updating agent opinion over world
			auto agentPpt = agent.opinions.property( _doorEntityGuid );
			if( agentPpt )
			{
				agentPpt->value = true;
			}

			// updating world property
			auto worldPpt = agent.world()->property( _doorEntityGuid );
			if( worldPpt )
			{
				worldPpt->value = true;
			}

			// all went good, outcome set properly
			bool allGood = agentPpt && worldPpt;

			return allGood;
		}

		gie::Action::State tick( gie::Agent& agent ) override
		{
			return Done;
		}
	};
	// Defining a move action as well
	class MoveAction : public gie::Action
	{
		gie::Guid _targetLocationPptGuid{ gie::NullGuid };

	public:
		MoveAction() = delete;
		MoveAction( gie::Guid targetLocationPptGuid ) : _targetLocationPptGuid( targetLocationPptGuid ) { }
		~MoveAction() = default;

		gie::StringHash actionName() const { return gie::stringHasher( "Move" ); }

		bool outcome( gie::Agent& agent ) override
		{
			// geting target location
			auto locationPpt = agent.world()->property( _targetLocationPptGuid );
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

		gie::Action::State tick( gie::Agent& agent ) override
		{
			return Done;
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

	// setting available actions
	//planner.actionSet().emplace_back(  );

	
	return 0;
}

int main( int argc, char** argv )
{
	return basicGoal();
}