#include <cstdio>
#include <cstdlib>
#include <vector>

#include <goapie_main.h>

#include "example.h"

// example 1
extern int fundamentals( ExampleParameters& params );
// example 2
extern int openDoor( ExampleParameters& params );
// example 3
extern int cutDownTrees( ExampleParameters& params );
// example 4
extern int treesOnHill( ExampleParameters& params );

// used to draw elements using OpenGL
extern int visualization( ExampleParameters& params );
void drawGoapieVisualizationWindow( bool& useHeuristics, ExampleParameters& params );
void drawImGuiWindows( bool& useHeuristics, ExampleParameters& params );
void drawWorldViewWindow();
// used to print actions from simulation leaf nodes
extern void printSimulatedActions( const gie::Planner& planner );

int main( int argc, char** argv )
{
	typedef int ( *ExampleFunc )( ExampleParameters& );

	std::vector< ExampleFunc > funcs
	{
		fundamentals,
		openDoor,
		cutDownTrees,
		treesOnHill
	};

	int ex = -1;

	if( argc > 1 )
	{
		ex = std::atoi( argv[ 1 ] );
	}
	
	if( ex < 1 || ex > funcs.size() )
	{
		while( true )
		{
			std::printf( "Enter valid example number [1..%d]: ", static_cast< int >( funcs.size() ) );
			std::scanf( "%d", &ex );

			if( ex < 1 || ex > funcs.size() )
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
	int exResult = funcs[ ex - 1 ]( exampleParams );

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