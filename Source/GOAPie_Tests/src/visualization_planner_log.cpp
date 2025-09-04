#include "visualization.h"

#include <string>

void drawPlannerLogWindow( ExampleParameters& params )
{
    if( !g_ShowPlannerLogWindow ) return;

    gie::Planner& planner = params.planner;

    if( ImGui::Begin( "Planner Log", &g_ShowPlannerLogWindow ) )
    {
        if( g_IsLoading ) { DrawWindowLoadingOverlay(); ImGui::End(); return; }
        const std::string& logContent = planner.logContent();

        if( logContent.empty() )
        {
            ImGui::Text( "No log content available." );
        }
        else
        {
            ImGui::TextUnformatted( logContent.c_str() );
        }
    }
    ImGui::End();
}
