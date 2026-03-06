#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

#include <goapie_main.h>

#include "example.h"

extern void RunLuaIntegrationTest();
extern void RunLuaEditorIntegrationTest();
extern int RunAutomatedTests();

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
	ExampleFunc luaFunc;		// optional Lua variant (nullptr if none)
	const char* luaFuncName;	// optional Lua variant name
};

// used to draw elements using OpenGL
extern int visualization( ExampleParameters& params );

// used to print actions from simulation leaf nodes
extern void printSimulatedActions( const gie::Planner& planner );

// TODO: Rename exampleFunctions to projectFunctions (frontend mapping)
std::vector< ExampleFunctionEntry > exampleFunctions{
	{ fundamentals,			fundamentalsName, fundamentalsDescription(),		nullptr, nullptr },
	{ openDoor,			openDoorName, openDoorDescription(),			nullptr, nullptr },
	{ cutDownTrees,			cutDownTreesName, cutDownTreesDescription(),		nullptr, nullptr },
	{ treesOnHill,			treesOnHillName, treesOnHillDescription(),		nullptr, nullptr },
	{ survivalOnHill,		survivalOnHillName, survivalOnHillDescription(),	nullptr, nullptr },
	{ heistOpenSafe,		heistOpenSafeName, heistOpenSafeDescription(),		heistOpenSafe_Lua, heistOpenSafe_LuaName },
};

static void printUsage()
{
	std::string msg;
	msg.reserve( 512 );
	msg += "usage: <exe> [options]\n\n";
	msg += "Running with no arguments will execute the automated test suite.\n\n";
	msg += "OPTIONS\n";
	msg += "    -e, --example <N>       Run native example numbered N (1.." + std::to_string( exampleFunctions.size() ) + ") and exit.\n";
	msg += "    -lua                    Use the Lua variant of the selected example.\n";
	msg += "    -v, --visualization     Launch visualization GUI. If combined with -e,\n";
	msg += "                            the example will be loaded before showing GUI.\n";
	msg += "    -le, --list-examples    List available examples with short descriptions.\n";
	msg += "    -t, --tests             Run Lua integration tests before app starts.\n\n";
	msg += "EXAMPLES\n";
	msg += "    <exe> -e 1               Run example 1 and exit.\n";
	msg += "    <exe> -e 2 -v            Load example 2 and show GUI.\n";
	msg += "    <exe> -e 6 -lua          Run the Lua variant of example 6.\n";
	msg += "    <exe> -e 6 -lua -v       Load Lua variant of example 6 and show GUI.\n";
	msg += "    <exe> -v                 Show GUI without loading any example.\n";
	msg += "    <exe> -t                 Run Lua tests and exit.\n";
	msg += "    <exe> -t -v              Run Lua tests then show GUI.\n";

	std::printf( "%s", msg.c_str() );
}

int main( int argc, char** argv )
{
	int ex = -1;
	bool visualize = false;
	bool listExamples = false;
	bool runTests = false;
	bool useLua = false;

	// Simple command-line parsing for -e/--example, -v/--visualization, -le/--list-examples and -t/--tests
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
		else if( std::strcmp( argv[ i ], "-lua" ) == 0 )
		{
			useLua = true;
		}
		else if( std::strcmp( argv[ i ], "-t" ) == 0 || std::strcmp( argv[ i ], "--tests" ) == 0 )
		{
			runTests = true;
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
			std::printf( "  %zu) %s%s\n", i + 1, exampleFunctions[ i ].name,
				exampleFunctions[ i ].luaFunc ? " [lua]" : "" );
			if( exampleFunctions[ i ].description )
			{
				std::printf( "     %s\n", exampleFunctions[ i ].description );
			}
		}
		return 0;
	}

	// Run Lua integration tests if requested (before app begins)
	if( runTests )
	{
		RunLuaIntegrationTest();
		RunLuaEditorIntegrationTest();
	}

	// If neither flag is provided, run automated tests
	if( ex == -1 && !visualize && !runTests )
	{
		return RunAutomatedTests();
	}

	if( runTests && ex == -1 && !visualize )
	{
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

		if( useLua && !exampleFunctions[ ex - 1 ].luaFunc )
		{
			std::printf( "Example %d (%s) does not have a Lua variant.\n", ex, exampleFunctions[ ex - 1 ].name );
			return 1;
		}
	}
	else if( useLua )
	{
		std::printf( "-lua requires an example number (-e N).\n" );
		printUsage();
		return 1;
	}

	// instantiating essential objects for any possible usage
	gie::World world{};
	gie::Planner planner{};
	gie::Goal goal{ world };

	// running example function if requested
	ExampleParameters exampleParams{ world, planner, goal };

	if( ex != -1 )
	{
		auto& entry = exampleFunctions[ ex - 1 ];
		if( useLua )
		{
			g_exampleName = entry.luaFuncName;
			int exResult = entry.luaFunc( exampleParams );
			( void )exResult;
		}
		else
		{
			g_exampleName = entry.name;
			int exResult = entry.func( exampleParams );
			( void )exResult;
		}
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
			planner.plan( true );

			// print the planned (winning) action sequence
			std::printf( "=== Planned Actions (A* result) ===\n" );
			auto& planned = planner.planActions();
			if( planned.empty() )
			{
				std::printf( "No plan found (goal not reached within depth limit).\n" );
			}
			else
			{
				for( auto it = planned.rbegin(); it != planned.rend(); ++it )
				{
					if( *it )
				{
					auto n = ( *it )->name();
					std::printf( "  -> %.*s\n", (int)n.size(), n.data() );
				}
				}
			}
			std::printf( "===================================\n" );

			// print total simulations explored
			std::printf( "Total simulations: %zu\n", planner.simulations().size() );

			// print simulation tree info for debugging
			size_t maxDepth = 0;
			size_t leafCount = 0;
			for( auto& [guid, sim] : planner.simulations() )
			{
				if( sim.depth > maxDepth ) maxDepth = sim.depth;
				if( sim.outgoing.empty() ) leafCount++;
			}
			std::printf( "Max depth reached: %zu, Leaf nodes: %zu\n", maxDepth, leafCount );
			// Print first few simulations with cost/heuristic
			size_t printed = 0;
			for( auto& [guid, sim] : planner.simulations() )
			{
				if( printed++ >= 20 ) break;
				std::printf( "  depth=%zu cost=%.1f h=%.1f f=%.1f actions=%zu\n",
					sim.depth, sim.cost, sim.heuristic.value,
					sim.cost + sim.heuristic.value, sim.actions.size() );
			}

			// print leaf simulation paths
			printSimulatedActions( planner );
		}
	}

	return 0;
}
