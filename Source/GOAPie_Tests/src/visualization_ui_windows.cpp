#include "visualization.h"

#include <algorithm>
#include <string>
#include <vector>
#include <cstring>

// Local state for Entity Outliner name dialog (Add/Rename)
namespace {
    enum class NameDialogMode : uint8_t { None = 0, Add, Rename };
    static NameDialogMode s_NameDialogMode = NameDialogMode::None;
    static char s_NameDialogBuf[256] = { 0 };
    static bool s_NameDialogFocus = false; // request keyboard focus on next frame

    inline void CancelNameDialog()
    {
        s_NameDialogMode = NameDialogMode::None;
        s_NameDialogBuf[0] = '\0';
        s_NameDialogFocus = false;
    }
}

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

// New: Entity Outliner implementation
void drawEntityOutlinerWindow( gie::World& world )
{
    if( !g_ShowEntityOutlinerWindow ) return;

    if( ImGui::Begin( "Entity Outliner", &g_ShowEntityOutlinerWindow ) )
    {
        // If name dialog is open and user interacts/focuses elsewhere, cancel it
        if( s_NameDialogMode != NameDialogMode::None )
        {
            if( !ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) )
            {
                ImGuiIO& io = ImGui::GetIO();
                if( io.MouseClicked[0] || io.MouseClicked[1] || io.MouseClicked[2] || io.MouseWheel != 0.0f )
                {
                    CancelNameDialog();
                }
            }
        }

        // Sync selection from other tools -> outliner
        if( g_WaypointEditSelectedGuid != gie::NullGuid && g_SelectedEntityGuid != g_WaypointEditSelectedGuid )
        {
            g_SelectedEntityGuid = g_WaypointEditSelectedGuid;
        }

        // Controls
        bool hasSelection = ( g_SelectedEntityGuid != gie::NullGuid && world.entity( g_SelectedEntityGuid ) != nullptr );

        ImGui::BeginDisabled( s_NameDialogMode != NameDialogMode::None );
        if( ImGui::Button( "+ Add" ) )
        {
            // Start name dialog for Add
            s_NameDialogMode = NameDialogMode::Add;
            s_NameDialogBuf[0] = '\0';
            s_NameDialogFocus = true;
        }
        ImGui::SameLine();

        ImGui::BeginDisabled( !hasSelection );
        if( ImGui::Button( "Delete" ) )
        {
            if( hasSelection )
            {
                world.removeEntity( g_SelectedEntityGuid );
                if( g_WaypointEditSelectedGuid == g_SelectedEntityGuid )
                {
                    g_WaypointEditSelectedGuid = gie::NullGuid;
                }
                g_SelectedEntityGuid = gie::NullGuid;
            }
        }
        ImGui::SameLine();
        if( ImGui::Button( "Rename" ) )
        {
            if( hasSelection )
            {
                s_NameDialogMode = NameDialogMode::Rename;
                auto* e = world.entity( g_SelectedEntityGuid );
                std::string curName = ( e && e->nameHash() != gie::InvalidStringHash ) ? std::string( gie::stringRegister().get( e->nameHash() ) ) : std::string{};
                std::strncpy( s_NameDialogBuf, curName.c_str(), sizeof( s_NameDialogBuf ) - 1 );
                s_NameDialogBuf[ sizeof( s_NameDialogBuf ) - 1 ] = '\0';
                s_NameDialogFocus = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        // Name dialog row (shared for Add/Rename)
        if( s_NameDialogMode != NameDialogMode::None )
        {
            ImGui::Separator();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( "Name:" );
            ImGui::SameLine();
            ImGui::SetNextItemWidth( 240.0f );
            if( s_NameDialogFocus ) { ImGui::SetKeyboardFocusHere(); s_NameDialogFocus = false; }
            bool apply = ImGui::InputText( "##entity_name_dialog", s_NameDialogBuf, IM_ARRAYSIZE( s_NameDialogBuf ), ImGuiInputTextFlags_EnterReturnsTrue );
            ImGui::SameLine();
            if( ImGui::Button( "Apply" ) ) apply = true;
            ImGui::SameLine();
            if( ImGui::Button( "Cancel" ) )
            {
                CancelNameDialog();
            }
            if( apply )
            {
                if( s_NameDialogMode == NameDialogMode::Rename )
                {
                    auto* e = world.entity( g_SelectedEntityGuid );
                    if( e )
                    {
                        e->setName( s_NameDialogBuf );
                    }
                }
                else if( s_NameDialogMode == NameDialogMode::Add )
                {
                    if( auto* e = world.createEntity( s_NameDialogBuf[0] ? s_NameDialogBuf : "entity" ) )
                    {
                        g_SelectedEntityGuid = e->guid();
                        g_WaypointEditSelectedGuid = g_SelectedEntityGuid; // sync
                    }
                }
                CancelNameDialog();
            }
            ImGui::Separator();
        }

        // Build sorted list of entities
        std::vector<std::pair<gie::Guid, const gie::Entity*>> entries;
        entries.reserve( world.context().entities().size() );
        for( const auto& kv : world.context().entities() )
        {
            entries.emplace_back( kv.first, &kv.second );
        }
        auto nameOf = []( const gie::Entity* e ) -> std::string
        {
            if( !e ) return std::string{};
            auto nh = e->nameHash();
            if( nh != gie::InvalidStringHash ) return std::string( gie::stringRegister().get( nh ) );
            return std::string{};
        };
        std::sort( entries.begin(), entries.end(), [&]( const auto& a, const auto& b )
        {
            std::string an = nameOf( a.second );
            std::string bn = nameOf( b.second );
            // Empty names go last
            if( an.empty() && bn.empty() ) return a.first < b.first;
            if( an.empty() ) return false;
            if( bn.empty() ) return true;
            if( an == bn ) return a.first < b.first;
            return an < bn;
        } );

        // List header
        if( ImGui::BeginTable( "##entity_outliner_table", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp ) )
        {
            ImGui::TableSetupColumn( "Entity" );
            ImGui::TableSetupColumn( "Guid" );
            ImGui::TableHeadersRow();

            for( const auto& [ guid, ent ] : entries )
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                std::string displayName = nameOf( ent );
                if( displayName.empty() ) displayName = "<unnamed>";
                bool selected = ( g_SelectedEntityGuid == guid );
                std::string selLabel = displayName + "##" + std::to_string( static_cast<unsigned long long>( guid ) );
                if( ImGui::Selectable( selLabel.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns ) )
                {
                    g_SelectedEntityGuid = guid;
                    g_WaypointEditSelectedGuid = guid; // sync to other tools
                }

                ImGui::TableSetColumnIndex( 1 );
                ImGui::Text( "%llu", static_cast<unsigned long long>( guid ) );
            }
            ImGui::EndTable();
        }

        // Hint
        ImGui::Separator();
        ImGui::TextUnformatted( "Tip: Click a row to select. 'Add' opens a name dialog." );
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

// New: Reset helper for Entity Outliner naming state
void ResetEntityOutlinerState()
{
    CancelNameDialog();
}
