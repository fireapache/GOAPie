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
	// defining available action
	class OpenDoor : public gie::ActionSimulator
	{
		gie::Guid _doorEntityGuid{ gie::NullGuid };

	public:
		OpenDoor() = delete;
		OpenDoor( const gie::World& world, gie::Guid doorEntityGuid ) : _doorEntityGuid( doorEntityGuid ) { }
		OpenDoor( OpenDoor&& ) = default;
		~OpenDoor() = default;

		bool prerequisites( const gie::Simulation& context, const gie::Agent& agent ) override
		{

		}

		//float cost( const gie::Agent& agent ) const override
		//{
		//	auto entityPtr = agent.world()->entity( _doorEntityGuid ); 
		//	// not valid if no target door entity is found
		//	if( !entityPtr )
		//	{
		//		return gie::MaxCost;
		//	}

		//	auto openedPpt = entityPtr->property( "Opened" );
		//	// not valid if no property "Opened" exists
		//	if( !openedPpt.second )
		//	{
		//		return gie::MaxCost;
		//	}

		//	// not valid if door is already opened
		//	if( openedPpt.second->getBool().second == true )
		//	{
		//		return gie::MaxCost;
		//	}

		//	constexpr float baseCost = 10.f;

		//	// returning distance cost in case there are location properties
		//	auto doorLocationPpt = entityPtr->property( "Location" );
		//	auto agentLocationPpt = agent.property( "Location" );
		//	if( doorLocationPpt.second && agentLocationPpt.second )
		//	{
		//		const glm::vec3 doorLocation = std::get< glm::vec3 >( doorLocationPpt.second->value );
		//		const glm::vec3 agentLocation = std::get< glm::vec3 >( agentLocationPpt.second->value );
		//		const float dist = glm::distance( doorLocation, agentLocation );
		//		return baseCost + dist;
		//	}

		//	return baseCost;
		//}
	};
	// creating goal
	gie::Goal goal{ world };
	// setting goal targets (door must get opened)
	goal.targets.emplace_back( openedGuid, true );
	// creating planner
	gie::Planner planner{ world, goal };
	// setting available actions
	planner.actionSet().emplace_back( OpenDoor{ world, entityGuid } );
	// creating agent
	auto [ agentGuid, agentPtr ] = world.createAgent();
	
	
	return 0;
}

int main( int argc, char** argv )
{
	return basicGoal();
}