#include "visualization.h"

void drawWorldSettingsWindow( ExampleParameters& params )
{
    if( !g_ShowWorldSettingsWindow ) return;

    if( ImGui::Begin( "World Settings", &g_ShowWorldSettingsWindow ) )
    {
        if( g_IsLoading ) { DrawWindowLoadingOverlay(); ImGui::End(); return; }
        // Gameplay examples render their imGuiDrawFunc in GOAPie Visualization instead
        if( params.isGameplayExample )
        {
            ImGui::TextUnformatted( "Gameplay example — see GOAPie Visualization window." );
        }
        else if( params.imGuiDrawFunc )
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
