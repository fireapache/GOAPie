#include "visualization.h"

#include <string>

void drawSimulationArgumentsWindow( ExampleParameters& params )
{
    if( !g_ShowSimulationArgumentsWindow ) return;

    const gie::Simulation* selectedSimulation = params.planner.simulation( selectedSimulationGuid );

    if( !selectedSimulation )
    {
        if( ImGui::Begin( "Simulation Arguments", &g_ShowSimulationArgumentsWindow ) )
        {
            ImGui::TextUnformatted( "No simulation selected." );
        }
        ImGui::End();
        return;
    }

    const auto& arguments = selectedSimulation->arguments();

    if( ImGui::Begin( "Simulation Arguments", &g_ShowSimulationArgumentsWindow ) )
    {
        if( arguments.empty() )
        {
            ImGui::Text( "No arguments available." );
        }
        else
        {
            for( const auto& [ key, value ] : arguments.storage() )
            {
                gie::Property ppt( 0, 0 );
                ppt.value = value;
                ImGui::Text( "Key: %s", gie::stringRegister().get( key ).data() );
                ImGui::Text( "Value: %s", ppt.toString().c_str() );
                ImGui::Separator();
            }
        }
    }
    ImGui::End();
}
