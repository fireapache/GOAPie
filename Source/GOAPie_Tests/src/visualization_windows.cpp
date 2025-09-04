#include "visualization.h"

void drawImGuiWindows( bool& useHeuristics, ExampleParameters& params )
{
    static bool s_prevShowWaypointEditorWindow = false;
    if( s_prevShowWaypointEditorWindow && !g_ShowWaypointEditorWindow )
    {
        ResetWaypointEditorState();
    }
    s_prevShowWaypointEditorWindow = g_ShowWaypointEditorWindow;

    // Track outliner visibility to cancel name dialog when closed or toggled off
    static bool s_prevShowEntityOutlinerWindow = false;
    if( s_prevShowEntityOutlinerWindow && !g_ShowEntityOutlinerWindow )
    {
        ResetEntityOutlinerState();
    }
    s_prevShowEntityOutlinerWindow = g_ShowEntityOutlinerWindow;

    // Draw tools
    drawGoapieVisualizationWindow( useHeuristics, params );
    drawWorldViewWindow( params.world, params.planner );
    drawDebugMessagesWindow( params );
    drawSimulationArgumentsWindow( params );
    drawPlannerLogWindow( params );
    drawDebugPathWindow( params );
    drawWaypointEditorWindow( params.world, params.planner );
    drawEntityOutlinerWindow( params.world ); // New
    drawDetailsPanelWindow( params.world );   // New
    drawWorldSettingsWindow( params );        // New: host example-specific ImGui funcs
}
