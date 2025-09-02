#include "visualization.h"

#include <algorithm>
#include <string>

static void drawEntityNameText( const gie::Entity& entity, const gie::Guid entityGuid, const bool padding )
{
    auto nameHash = entity.nameHash();
    const bool isValidName = nameHash != gie::InvalidStringHash;
    const std::string_view entityName = isValidName ? gie::stringRegister().get( nameHash ) : "Unnamed Entity";

    if( padding )
    {
        ImGui::SetCursorPosX( ( ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( entityName.data() ).x ) * 0.225f );
    }

    if( isValidName )
    {
        ImGui::Text( "%s", entityName.data() );
    }
    else
    {
        ImGui::Text( "%llu", static_cast< unsigned long long >( entityGuid ) );
    }
}

void drawBlackboardPropertiesWindow( const gie::Simulation* simulation )
{
    if( !g_ShowBlackboardPropertiesWindow ) return;
    if( !simulation ) return;

    const gie::Blackboard* currentContext = &simulation->context();
    std::set< gie::Guid > entityGuids;
    const auto& stringregister = gie::stringRegister();

    while( currentContext )
    {
        for( const auto& [ entityGuid, entity ] : currentContext->entities() )
        {
            entityGuids.insert( entityGuid );
        }

        currentContext = currentContext->parent();
    }

    static bool multiLevel = false;

    if( ImGui::Begin( "Blackboard Properties", &g_ShowBlackboardPropertiesWindow ) )
    {
        currentContext = &simulation->context();
        ImGui::Checkbox( "Multi-Level", &multiLevel );

        if( multiLevel )
        {
            int level = 0;

            while( currentContext )
            {
                ImGui::Text( "Blackboard Level: %d", level );
                ImGui::Separator();

                for( const auto& [ entityGuid, entity ] : currentContext->entities() )
                {
                    drawEntityNameText( entity, entityGuid, true );
                    ImGui::Separator();

                    ImGui::Columns( 2, nullptr, false );
                    for( const auto& [ propertyNameHash, propertyGuid ] : entity.properties() )
                    {
                        auto propertyName = stringregister.get( propertyNameHash );
                        auto property = currentContext->property( propertyGuid );
                        ImGui::Text( "%s", propertyName.data() );
                        ImGui::NextColumn();
                        ImGui::Text( "%s", property->toString().c_str() );
                        ImGui::NextColumn();
                    }
                    ImGui::Columns( 1 );
                    ImGui::Separator();
                }

                currentContext = currentContext->parent();
                level++;
                ImGui::Separator();
            }
        }
        else
        {
            for( auto entityGuid : entityGuids )
            {
                auto entity = currentContext->entity( entityGuid );

                drawEntityNameText( *entity, entityGuid, true );
                ImGui::Separator();

                ImGui::Columns( 2, nullptr, false );
                for( const auto& [ propertyNameHash, propertyGuid ] : entity->properties() )
                {
                    auto propertyName = stringregister.get( propertyNameHash );
                    auto property = currentContext->property( propertyGuid );
                    ImGui::Text( "%s", propertyName.data() );
                    ImGui::NextColumn();
                    ImGui::Text( "%s", property->toString().c_str() );
                    ImGui::NextColumn();
                }
                ImGui::Columns( 1 );
                ImGui::Separator();
            }
        }
    }
    ImGui::End();
}

void drawGoapieVisualizationWindow( bool& useHeuristics, ExampleParameters& params )
{
    if( !g_ShowGoapieVisualizationWindow ) return;

    gie::World& world = params.world;
    gie::Planner& planner = params.planner;
	static int simultationDepth = 10;

    if( ImGui::Begin( "GOAPie Visualization", &g_ShowGoapieVisualizationWindow ) )
    {
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

            if( params.imGuiDrawFunc )
            {
                params.imGuiDrawFunc( world, planner, params.goal, selectedSimulationGuid );
                ImGui::Separator();
            }

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

void drawDebugMessagesWindow( ExampleParameters& params )
{
    if( !g_ShowDebugMessagesWindow ) return;

    const gie::Simulation* selectedSimulation = params.planner.simulation( selectedSimulationGuid );

    if( !selectedSimulation )
    {
        if( ImGui::Begin( "Debug Messages", &g_ShowDebugMessagesWindow ) )
        {
            ImGui::TextUnformatted( "No simulation selected." );
        }
        ImGui::End();
        return;
    }

    const auto& debugMessages = selectedSimulation->debugMessages();

    if( ImGui::Begin( "Debug Messages", &g_ShowDebugMessagesWindow ) )
    {
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

void drawPlannerLogWindow( ExampleParameters& params )
{
    if( !g_ShowPlannerLogWindow ) return;

    gie::Planner& planner = params.planner;

    if( ImGui::Begin( "Planner Log", &g_ShowPlannerLogWindow ) )
    {
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

void drawImGuiWindows( bool& useHeuristics, ExampleParameters& params )
{
    static bool s_prevShowWaypointEditorWindow = false;
    if( s_prevShowWaypointEditorWindow && !g_ShowWaypointEditorWindow )
    {
        ResetWaypointEditorState();
    }
    s_prevShowWaypointEditorWindow = g_ShowWaypointEditorWindow;

    drawGoapieVisualizationWindow( useHeuristics, params );
    drawWorldViewWindow( params.world, params.planner );
    drawDebugMessagesWindow( params );
    drawSimulationArgumentsWindow( params );
    drawPlannerLogWindow( params );
    drawDebugPathWindow( params );
    drawWaypointEditorWindow( params.world, params.planner );
}

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

void ResetWaypointEditorState()
{
    g_WaypointEditSelectedGuid = gie::NullGuid;
    g_WaypointEditPlaceArmed = false;
    g_WaypointEditHasTargetWorldPos = false;
    g_WaypointDragActive = false;
}
