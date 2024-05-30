#include "goapie.h"

int insertingDataEntries()
{
	gie::World world;
	gie::Guid lastEntryGuid{ gie::NullGuid };

	for( size_t i = 0; i < 10; i++ )
	{
		lastEntryGuid = world.context().registerEntry();
	}
	
	auto entry = world.context().entry( lastEntryGuid );

	return 0;
}

int insertingParameters()
{
	gie::World world;
	auto entryGuid = world.context().registerEntry();
	auto entry = world.context().entry( entryGuid );

	if( !entry )
	{
		return 1;
	}

	auto [ param1Guid, param1Ptr ] = entry->registerProperty( "AmmoCount" );

	if( param1Ptr )
	{
		param1Ptr->value = 5;
	}

	auto [ param2Guid, param2Ptr ] = entry->registerProperty( "Name" );

	if( param2Ptr )
	{
		param2Ptr->value = gie::stringHasher( "BFG" );
	}

	return 0;
}

int main( int argc, char** argv )
{
	return insertingParameters();
}