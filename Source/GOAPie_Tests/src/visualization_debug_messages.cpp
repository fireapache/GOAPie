#include "visualization.h"

#include <string>

void drawDebugMessagesWindow( ExampleParameters& params )
{
    if( !g_ShowDebugMessagesWindow ) return;

    const gie::Simulation* selectedSimulation = params.planner.simulation( selectedSimulationGuid );

    if( !selectedSimulation )
    {
        if( ImGui::Begin( "Debug Messages", &g_ShowDebugMessagesWindow ) )
        {
            if( g_IsLoading ) { DrawWindowLoadingOverlay(); ImGui::End(); return; }
            ImGui::TextUnformatted( "No simulation selected." );
        }
        ImGui::End();
        return;
    }

    const auto& debugMessages = selectedSimulation->debugMessages();

    if( ImGui::Begin( "Debug Messages", &g_ShowDebugMessagesWindow ) )
    {
        if( g_IsLoading ) { DrawWindowLoadingOverlay(); ImGui::End(); return; }
        if( !debugMessages.messages() || debugMessages.messages()->empty() )
        {
            ImGui::Text( "No debug messages available." );
        }
        else
        {
            for( const auto& message : *debugMessages.messages() )
            {
                const bool hasScope = message.find( "::" ) != std::string::npos;
                if( hasScope )
                {
                    ImGui::TextColored( ImColor{ 1.0f, 0.6f, 0.0f, 1.0f }, "* %s", message.c_str() );
                }
                else
                {
                    ImGui::TextUnformatted( message.c_str() );
                }
            }
        }
    }
    ImGui::End();
}
