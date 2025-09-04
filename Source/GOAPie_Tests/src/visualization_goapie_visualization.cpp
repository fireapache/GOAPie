#include "visualization.h"

#include <string>

void drawSimulationTreeView( const gie::Planner& planner, const gie::Simulation* simulation )
{
    if( !simulation )
        return;

    ImVec2 oldPadding = ImGui::GetStyle().FramePadding;
    ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 1.0f, oldPadding.y ) );

    float oldIndent = ImGui::GetStyle().IndentSpacing;
    ImGui::PushStyleVar( ImGuiStyleVar_IndentSpacing, 6.0f );

    std::string actionsText{ "" };
    for( auto action : simulation->actions )
    {
        if( action )
        {
            actionsText.append( action->name() );
            if( action != simulation->actions.back() )
            {
                actionsText.append( ", " );
            }
        }
    }

    if( actionsText.empty() )
    {
        actionsText = "Root";
    }

    std::string label = actionsText + "##" + std::to_string( simulation->guid() );

    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DrawLinesToNodes | ImGuiTreeNodeFlags_SpanAvailWidth;
    if( selectedSimulationGuid == simulation->guid() )
        nodeFlags |= ImGuiTreeNodeFlags_Selected;

    bool nodeOpen = ImGui::TreeNodeEx( label.c_str(), nodeFlags );
    if( ImGui::IsItemClicked() )
    {
        selectedSimulationGuid = simulation->guid();
        g_PathStepMode = false;
        g_PathStepIndex = 0;
        g_PathStepSimGuid = gie::NullGuid;
    }

    if( nodeOpen )
    {
        for( auto childSimulationGuid : simulation->outgoing )
        {
            auto childSimulation = planner.simulation( childSimulationGuid );
            if( childSimulation )
            {
                drawSimulationTreeView( planner, childSimulation );
            }
        }
        ImGui::TreePop();
    }

    ImGui::PopStyleVar( 2 );
}

void drawGoapieVisualizationWindow( bool& useHeuristics, ExampleParameters& params )
{
    if( !g_ShowGoapieVisualizationWindow ) return;

    gie::World& world = params.world;
    gie::Planner& planner = params.planner;
    static int simultationDepth = 10;

    if( ImGui::Begin( "GOAPie Visualization", &g_ShowGoapieVisualizationWindow ) )
    {
        if( g_IsLoading )
        {
            DrawWindowLoadingOverlay();
            ImGui::End();
            return;
        }

        ImGui::Checkbox( "Use Heuristics", &useHeuristics );
        ImGui::Checkbox( "Log Plan", &planner.logStepsMutator() );
        ImGui::SliderInt( "Depth Limit", &simultationDepth, 10, 50 );
        planner.depthLimitMutator() = static_cast< size_t >( simultationDepth );

        ImGui::Separator();
        if( ImGui::Button( g_ShowMorePlannerOptions ? "Less" : "More" ) )
        {
            g_ShowMorePlannerOptions = !g_ShowMorePlannerOptions;
        }
        if( g_ShowMorePlannerOptions )
        {
            ImGui::Indent();
            ImGui::Checkbox( "Step Plan", &planner.stepMutator() );
            ImGui::Checkbox( "Show Waypoint IDs (last 4)", &g_ShowWaypointGuidSuffix );
            ImGui::Checkbox( "Show Waypoint Arrows", &g_ShowWaypointArrows );
            ImGui::Unindent();
        }
        if( planner.isReady() )
        {
            if( ImGui::Button( "Plan!" ) )
            {
                planner.plan( useHeuristics );
            }
        }
        else
        {
            ImGui::PushStyleVar( ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f );
            ImGui::Button( "Plan!" );
            ImGui::PopStyleVar();
            ImGui::SameLine();
            ImGui::Text( "Planner is busy!" );

            if( planner.stepMutator() && ImGui::Button( "Step" ) )
            {
                planner.plan( useHeuristics );
            }
        }

        auto waypointGuids = world.context().entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
        if( waypointGuids )
        {
            ImGui::Text( "Waypoint Count: %d", static_cast< int >( waypointGuids->size() ) );
        }

        if( ImGui::CollapsingHeader( "Simulation Nodes" ) )
        {
            auto selectedSim = planner.simulation( selectedSimulationGuid );
            ImGui::Separator();
            ImGui::Text( "Selected Node Info:" );
            if( selectedSim )
            {
                ImGui::Text( "Guid: %llu", selectedSim ? static_cast< unsigned long long >( selectedSim->guid() ) : 0 );
            }
            else
            {
                ImGui::TextUnformatted( "Guid: ?" );
            }
            ImGui::Text( "Actions: %zu", selectedSim ? selectedSim->actions.size() : 0 );
            const bool isMaxCost = selectedSim ? selectedSim->cost == gie::MaxCost : true;
            if( isMaxCost )
            {
                ImGui::TextUnformatted( "Cost: MAX" );
            }
            else if( selectedSim )
            {
                ImGui::Text( "Cost: %.2f", selectedSim ? selectedSim->cost : 0 );
            }
            else
            {
                ImGui::TextUnformatted( "Cost: ?" );
            }

            ImGui::Separator();

            auto rootNode = planner.rootSimulation();
            if( rootNode )
            {
                drawSimulationTreeView( planner, rootNode );
            }
            else
            {
                ImGui::Text( "No simulation nodes available." );
            }

            // separate window, toggled via Tools menu
            drawBlackboardPropertiesWindow( selectedSim );
        }
    }
    ImGui::End();
}
