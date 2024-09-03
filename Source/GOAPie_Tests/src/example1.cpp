#include "goapie.h"

int insertingDataEntities();
int insertingParameters();

int fundamentals()
{
	if( !insertingDataEntities() ) return 1;
	if( !insertingParameters() ) return 1;
	return 0;
}

int insertingDataEntities()
{
	gie::World world;
	gie::Guid lastEntityGuid{ gie::NullGuid };

	for( size_t i = 0; i < 10; i++ )
	{
		lastEntityGuid = world.createEntity()->guid();
	}
	
	auto entity = world.entity( lastEntityGuid );

	return 0;
}

int insertingParameters()
{
	gie::World world;
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