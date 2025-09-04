#include "visualization.h"

void drawWorldSettingsWindow( ExampleParameters& params )
{
    if( !g_ShowWorldSettingsWindow ) return;

    if( ImGui::Begin( "World Settings", &g_ShowWorldSettingsWindow ) )
    {
        if( g_IsLoading ) { DrawWindowLoadingOverlay(); ImGui::End(); return; }
        // If the example provided a draw function, render it here
        if( params.imGuiDrawFunc )
        {
            params.imGuiDrawFunc( params.world, params.planner, params.goal, selectedSimulationGuid );
        }
        else
        {
            ImGui::TextUnformatted( "No world settings available for this example." );
        }
    }
    ImGui::End();
}
