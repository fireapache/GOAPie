// Source/GOAPie_Tests/src/visualization_world_setup.cpp
// Compatibility wrapper forwarding old World Setup call to the new Planner Setup implementation.
// This preserves existing call sites that reference drawWorldSetupWindow(...).

#include "visualization.h"

// Forward declaration of the new Planner Setup function implemented in visualization_planner_setup.cpp
void drawPlannerSetupWindow( ExampleParameters& params );

// Keep the original API for compatibility; forward to the Planner Setup implementation.
void drawWorldSetupWindow( ExampleParameters& params )
{
    drawPlannerSetupWindow( params );
}
