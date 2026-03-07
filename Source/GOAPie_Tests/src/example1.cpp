#include <goapie.h>

#include "example.h"

int insertingDataEntities( gie::World& world );
int insertingParameters( gie::World& world );

const char* fundamentalsDescription()
{
    return "Basic world/entity/property insertion example.";
}

int fundamentals( ExampleParameters& params )
{
	gie::World& world = params.world;
	gie::Planner& planner = params.planner;
	gie::Goal& goal = params.goal;

	if( !insertingDataEntities( world ) ) return 1;
	if( !insertingParameters( world ) ) return 1;
	return 0;
}

int insertingDataEntities( gie::World& world )
{
	gie::Guid lastEntityGuid{ gie::NullGuid };

	for( size_t i = 0; i < 10; i++ )
	{
		lastEntityGuid = world.createEntity()->guid();
	}
	
	auto entity = world.entity( lastEntityGuid );

	return 0;
}

int fundamentalsValidateResult( std::string& failMsg )
{
	gie::World world{};
	gie::Planner planner{};
	gie::Goal goal{ world };
	ExampleParameters params{ world, planner, goal };

	// Run the sub-functions directly to validate entity/property creation
	VALIDATE( insertingDataEntities( params.world ) == 0, "insertingDataEntities failed" );
	VALIDATE( insertingParameters( params.world ) == 0, "insertingParameters failed" );

	// insertingDataEntities creates 10 entities, insertingParameters creates 1 more = 11 total
	size_t entityCount = 0;
	for( auto& [guid, entity] : world.context().entities() )
	{
		entityCount++;
	}
	VALIDATE_EQ( entityCount, size_t( 11 ), "entity count mismatch" );

	return 0;
}

int insertingParameters( gie::World& world )
{
	auto entity = world.createEntity();

	if( !entity )
	{
		return 1;
	}

	auto ammoCountPpt = entity->createProperty( "AmmoCount" );
	if( ammoCountPpt )
	{
		ammoCountPpt->value = 5;
	}

	auto namePpt = entity->createProperty( "Name" );
	if( namePpt )
	{
		namePpt->value = gie::stringHasher( "BFG" );
	}

	return 0;
}