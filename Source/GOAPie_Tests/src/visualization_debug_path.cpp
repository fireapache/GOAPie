#include "visualization.h"

void drawDebugPathWindow( ExampleParameters& params )
{
    if( !g_ShowDebugPathWindow ) return;

    gie::Planner& planner = params.planner;
    const gie::Simulation* selectedSim = planner.simulation( selectedSimulationGuid );

    if( ImGui::Begin( "Debug Path", &g_ShowDebugPathWindow ) )
    {
        if( !selectedSim )
        {
            ImGui::TextUnformatted( "No simulation selected. Select a node in 'Simulation Nodes'." );
        }
        else
        {
            const auto* openedOffsetsArg = selectedSim->arguments().get( "PF_OpenedOffsets" );
            const auto* visitedOffsetsArg = selectedSim->arguments().get( "PF_VisitedOffsets" );
            const auto* backtrackOffsetsArg = selectedSim->arguments().get( "PF_BacktrackOffsets" );
            if( openedOffsetsArg && visitedOffsetsArg && backtrackOffsetsArg )
            {
                const auto& openedOffsets = std::get< gie::Property::IntegerVector >( *openedOffsetsArg );
                int statesCount = static_cast<int>( openedOffsets.size() > 0 ? openedOffsets.size() - 1 : 0 );

                ImGui::Text( "Path Steps: %d", statesCount );
                if( statesCount > 0 )
                {
                    if( ImGui::Button( "Step Path" ) )
                    {
                        if( !g_PathStepMode || g_PathStepSimGuid != selectedSim->guid() )
                        {
                            g_PathStepMode = true;
                            g_PathStepIndex = 0;
                            g_PathStepSimGuid = selectedSim->guid();
                        }
                        else
                        {
                            if( g_PathStepIndex + 1 < statesCount )
                            {
                                g_PathStepIndex++;
                            }
                            else
                            {
                                g_PathStepMode = false;
                                g_PathStepIndex = 0;
                                g_PathStepSimGuid = gie::NullGuid;
                            }
                        }
                    }
                    ImGui::SameLine();
                    ImGui::Text( "State: %d / %d", g_PathStepMode && g_PathStepSimGuid == selectedSim->guid() ? ( g_PathStepIndex + 1 ) : 0, statesCount );
                }
                else
                {
                    ImGui::TextUnformatted( "No step states available." );
                }
            }
            else
            {
                ImGui::TextUnformatted( "Selected simulation has no path step data." );
            }
        }
    }
    ImGui::End();
}
