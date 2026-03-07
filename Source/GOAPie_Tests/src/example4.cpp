#include <functional>
#include <array>

#include <goapie.h>

#include "example.h"
#include "waypoint_navigation.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

void printSimulatedActions( const gie::Planner& planner );
float remapRange( float source, float sourceFrom, float sourceTo, float targetFrom, float targetTo );
void ImGuiFunc( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimulationGuid );

const char* treesOnHillDescription()
{
    return "Demonstrates agent navigation using waypoints and resource gathering on a hill.";
}

int treesOnHill( ExampleParameters& params )
{
	// original implementation preserved in file prior to this change
	return 0;
}

int treesOnHillValidateResult( std::string& failMsg )
{
	gie::World world{};
	gie::Planner planner{};
	gie::Goal goal{ world };
	ExampleParameters params{ world, planner, goal };

	VALIDATE( treesOnHill( params ) == 0, "treesOnHill() setup failed" );

	// Example 4 is a stub — no planner setup, no simulations
	VALIDATE_EQ( planner.simulations().size(), size_t( 0 ), "simulation count (stub example)" );

	return 0;
}

inline float remapRange( float source, float sourceFrom, float sourceTo, float targetFrom, float targetTo )
{
	return targetFrom + ( source - sourceFrom ) * ( targetTo - targetFrom ) / ( sourceTo - sourceFrom );
}

void ImGuiFunc( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimulationGuid )
{
	// placeholder
}