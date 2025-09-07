#include <cstdio>
#include <cstdlib>
#include <vector>

#include <goapie_main.h>

#include "example.h"

// found in visualization.h
std::string g_exampleName;

#define EXAMPLE_FUNCTION( x ) \
extern int x( ExampleParameters& params ); \
const char* x##Name = #x; \

// example 1
EXAMPLE_FUNCTION( fundamentals )
// example 2
EXAMPLE_FUNCTION( openDoor )
// example 3
EXAMPLE_FUNCTION( cutDownTrees )
// example 4
EXAMPLE_FUNCTION( treesOnHill )
// example 5
EXAMPLE_FUNCTION( survivalOnHill )
 // example 6
EXAMPLE_FUNCTION( heistOpenSafe )
EXAMPLE_FUNCTION( heistOpenSafe_Lua )
typedef int ( *ExampleFunc )( ExampleParameters& );

struct ExampleFunctionEntry
{
	ExampleFunc func;
	const char* name;
};

// used to draw elements using OpenGL
extern int visualization( ExampleParameters& params );

// used to print actions from simulation leaf nodes
extern void printSimulatedActions( const gie::Planner& planner );

int main( int argc, char** argv )
{
std::vector< ExampleFunctionEntry > exampleFunctions
{
{ fundamentals, fundamentalsName },
{ openDoor, openDoorName },
{ cutDownTrees, cutDownTreesName },
{ treesOnHill, treesOnHillName },
{ survivalOnHill, survivalOnHillName },
{ heistOpenSafe, heistOpenSafeName },
{ heistOpenSafe_Lua, heistOpenSafe_LuaName }
};

	int ex = -1;

	if( argc > 1 )
	{
		ex = std::atoi( argv[ 1 ] );
	}
	
	if( ex < 1 || ex > exampleFunctions.size() )
	{
		while( true )
		{
			std::printf( "Enter valid example number [1..%d]: ", static_cast< int >( exampleFunctions.size() ) );
			std::scanf( "%d", &ex );

			if( ex < 1 || ex > exampleFunctions.size() )
			{
				std::printf( "Wrong example number!\n" );
			}
			else
			{
				break;
			}
		}
	}

	// checking for -v parameter
	bool visualize = false;
	for( int i = 1; i < argc; ++i )
	{
		if( std::strcmp( argv[ i ], "-v" ) == 0 )
		{
			visualize = true;
			break;
		}
	}
	
	// instantiating essential objects for the example
	gie::World world{};
	gie::Planner planner{};
	gie::Goal goal{ world };

	// running example function
	ExampleParameters exampleParams{ world, planner, goal };
	int exResult = exampleFunctions[ ex - 1 ].func( exampleParams );
	g_exampleName = exampleFunctions[ ex - 1 ].name;

	// example code set up the world, planner and goal,
	// and now we can run the planner.

	if( visualize )
	{
		// using opengl to visualize the plan and world
		return visualization( exampleParams );
	}
	else
	{
		// simply run the planner
		planner.plan();

		// printing simulated nodes
		printSimulatedActions( planner );
	}

	return 0;
}
