#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

#include <goapie_main.h>

#include "example.h"

// found in visualization.h
// TODO: Rename g_exampleName to g_projectName (frontend mapping)
std::string g_exampleName;

// TODO: Rename EXAMPLE_FUNCTION to PROJECT_FUNCTION (frontend mapping)
#define EXAMPLE_FUNCTION( x ) \
	extern int x( ExampleParameters& params ); \
	extern const char* x##Description(); \
	const char* x##Name = #x;

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
// TODO: Rename ExampleFunc to ProjectFunc (frontend mapping)
typedef int ( *ExampleFunc )( ExampleParameters& );

// TODO: Rename ExampleFunctionEntry to ProjectFunctionEntry (frontend mapping)
struct ExampleFunctionEntry
{
	ExampleFunc func;
	const char* name;
	const char* description;
};

// used to draw elements using OpenGL
extern int visualization( ExampleParameters& params );

// used to print actions from simulation leaf nodes
extern void printSimulatedActions( const gie::Planner& planner );

// TODO: Rename exampleFunctions to projectFunctions (frontend mapping)
std::vector< ExampleFunctionEntry > exampleFunctions{
	{ fundamentals,			fundamentalsName, fundamentalsDescription() },
	{ openDoor,			openDoorName, openDoorDescription() },
	{ cutDownTrees,			cutDownTreesName, cutDownTreesDescription() },
	{ treesOnHill,			treesOnHillName, treesOnHillDescription() },
	{ survivalOnHill,		survivalOnHillName, survivalOnHillDescription() },
	{ heistOpenSafe,		heistOpenSafeName, heistOpenSafeDescription() },
	{ heistOpenSafe_Lua,	heistOpenSafe_LuaName, heistOpenSafe_LuaDescription() }
};

static void printUsage()
{
	std::string msg;
	msg.reserve( 512 );
	msg += "usage: <exe> [options]\n\n";
	msg += "OPTIONS\n";
 msg += "    -e, --example <N>       Run native example numbered N (1.." + std::to_string( exampleFunctions.size() ) + ") and exit.\n";
 msg += "    -v, --visualization     Launch visualization GUI. If combined with -e,\n";
 msg += "                            the example will be loaded before showing GUI.\n";
 msg += "    -le, --list-examples    List available examples with short descriptions.\n\n";
	msg += "EXAMPLES\n";
	msg += "    <exe> -e 1               Run example 1 and exit.\n";
	msg += "    <exe> -e 2 -v            Load example 2 and show GUI.\n";
	msg += "    <exe> -v                 Show GUI without loading any example.\n";

	std::printf( "%s", msg.c_str() );
}

int main( int argc, char** argv )
{
	int ex = -1;
	bool visualize = false;
	bool listExamples = false;

	// Simple command-line parsing for -e/--example, -v/--visualization and -le/--list-examples
	for( int i = 1; i < argc; ++i )
	{
		if( std::strcmp( argv[ i ], "-v" ) == 0 || std::strcmp( argv[ i ], "--visualization" ) == 0 )
		{
			visualize = true;
		}
		else if( std::strcmp( argv[ i ], "-e" ) == 0 || std::strcmp( argv[ i ], "--example" ) == 0 )
		{
			if( i + 1 < argc )
			{
				ex = std::atoi( argv[ i + 1 ] );
				++i; // skip the number argument
			}
			else
			{
				std::printf( "Missing example number after %s\n", argv[ i ] );
				printUsage();
				return 1;
			}
		}
		else if( std::strcmp( argv[ i ], "-le" ) == 0 || std::strcmp( argv[ i ], "--list-examples" ) == 0 )
		{
			listExamples = true;
		}
		else
		{
			// ignore unknown parameters for now
		}
	}

	// If list-examples requested, print list and exit
	if( listExamples )
	{
		std::printf( "Available examples (use -e N to run):\n" );
		for( size_t i = 0; i < exampleFunctions.size(); ++i )
		{
			std::printf( "  %zu) %s\n", i + 1, exampleFunctions[ i ].name );
			if( exampleFunctions[ i ].description )
			{
				std::printf( "     %s\n", exampleFunctions[ i ].description );
			}
		}
		return 0;
	}

	// If neither flag is provided, print usage and quit
	if( ex == -1 && !visualize )
	{
		printUsage();
		return 0;
	}

	// validate example number if provided
	if( ex != -1 )
	{
		if( ex < 1 || static_cast<size_t>( ex ) > exampleFunctions.size() )
		{
			std::printf( "Wrong example number: %d (valid range 1..%zu)\n", ex, exampleFunctions.size() );
			printUsage();
			return 1;
		}
	}

	// instantiating essential objects for any possible usage
	gie::World world{};
	gie::Planner planner{};
	gie::Goal goal{ world };

	// running example function if requested
	ExampleParameters exampleParams{ world, planner, goal };

	if( ex != -1 )
	{
		g_exampleName = exampleFunctions[ ex - 1 ].name;
		int exResult = exampleFunctions[ ex - 1 ].func( exampleParams );
		( void )exResult;
	}
	else
	{
		// no example loaded
		g_exampleName.clear();
	}

	if( visualize )
	{
		// using opengl to visualize the plan and world
		return visualization( exampleParams );
	}
	else
	{
		if( ex != -1 )
		{
			// simply run the planner (example already set up)
			planner.plan();

			// printing simulated nodes
			printSimulatedActions( planner );
		}
	}

	return 0;
}
