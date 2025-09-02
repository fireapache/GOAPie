#include "visualization.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <set>

// Forward declarations for internal helpers
static void handleWaypointEditorOnWorldView( ImVec2 pos, float windowWidth, float windowHeight );
static void drawWaypointEditorOverlayOnWorldView( ImVec2 pos, float windowWidth, float windowHeight );

// New: generic entity selection from World View (agnostic to tools)
static void handleEntitySelectionOnWorldView( ImVec2 pos, float windowWidth, float windowHeight )
{
    if( !g_WorldPtr ) return;

    ImGuiIO& io = ImGui::GetIO();
    const float localX = io.MousePos.x - pos.x;
    const float localY = io.MousePos.y - pos.y;
    const bool mouseOverWindow = ( localX >= 0.0f && localX <= windowWidth && localY >= 0.0f && localY <= windowHeight );
    if( !mouseOverWindow ) return;

    // Only react on fresh left click when Waypoint Editor is not handling interactions
    if( g_ShowWaypointEditorWindow ) return; // let the waypoint tool handle selection when active
    if( !ImGui::IsMouseClicked( 0 ) ) return;

    // Pick nearest entity among those tagged for drawing and having a Location
    const auto* drawSet = g_WorldPtr->context().entityTagRegister().tagSet( { gie::stringHasher( "Draw" ) } );
    if( !drawSet || drawSet->empty() ) return;

    float bestDist2 = g_WaypointPickRadiusPx * g_WaypointPickRadiusPx; // reuse pick radius
    gie::Guid best = gie::NullGuid;

    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = g_DrawingLimits.scale;

    for( auto guid : *drawSet )
    {
        const auto* e = g_WorldPtr->entity( guid );
        if( !e ) continue;
        const auto* loc = e->property( "Location" );
        if( !loc || !loc->getVec3() ) continue;

        glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
        float x_px = ( p.x * 0.5f + 0.5f ) * windowWidth;
        float y_px = ( 1.0f - ( p.y * 0.5f + 0.5f ) ) * windowHeight;
        float dx = x_px - localX;
        float dy = y_px - localY;
        float d2 = dx * dx + dy * dy;
        if( d2 <= bestDist2 )
        {
            bestDist2 = d2;
            best = guid;
        }
    }

    if( best != gie::NullGuid )
    {
        g_SelectedEntityGuid = best;
        // Sync waypoint editor selection if it's a waypoint when tool opens later
        const auto* waypointSet = g_WorldPtr->context().entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
        if( waypointSet && waypointSet->find( best ) != waypointSet->end() )
        {
            g_WaypointEditSelectedGuid = best;
        }
    }
}

void drawWorldViewWindow( gie::World& world, const gie::Planner& planner )
{
    if( !g_ShowWorldViewWindow ) return;

    if( ImGui::Begin( "World View", &g_ShowWorldViewWindow ) )
    {
        const float windowWidth = ImGui::GetContentRegionAvail().x;
        const float windowHeight = ImGui::GetContentRegionAvail().y;

        rescale_framebuffer( static_cast< GLsizei >( windowWidth ), static_cast< GLsizei >( windowHeight ) );
        glViewport( 0, 0, static_cast< GLsizei >( windowWidth ), static_cast< GLsizei >( windowHeight ) );

        ImVec2 pos = ImGui::GetCursorScreenPos();

        ImGui::GetWindowDrawList()->AddImage(
            texture_id,
            ImVec2( pos.x, pos.y ),
            ImVec2( pos.x + windowWidth, pos.y + windowHeight ),
            ImVec2( 0, 1 ),
            ImVec2( 1, 0 ) );

        ImGui::SetCursorScreenPos( ImVec2( pos.x + 8.0f, pos.y + 8.0f ) );
        static float s_SaveMsgTimer = 0.0f;
        static float s_LoadMsgTimer = 0.0f;
        if( !g_BoundsEditorVisible )
        {
            if( ImGui::Button( "Update Bounds" ) )
            {
                if( !g_DrawingLimitsInitialized )
                {
                    updateDrawingBounds( world );
                }
                g_BoundsInputX[ 0 ] = g_DrawingLimits.minBounds.x;
                g_BoundsInputX[ 1 ] = g_DrawingLimits.maxBounds.x;
                g_BoundsInputY[ 0 ] = g_DrawingLimits.minBounds.y;
                g_BoundsInputY[ 1 ] = g_DrawingLimits.maxBounds.y;
                g_BoundsEditorVisible = true;
            }
            ImGui::SameLine();
            if( ImGui::Button( "Save" ) )
            {
                if( gie::persistency::SaveWorldToJson( world, "world.json" ) )
                {
                    s_SaveMsgTimer = 2.0f;
                }
            }
            ImGui::SameLine();
            if( ImGui::Button( "Load" ) )
            {
                if( gie::persistency::LoadWorldFromJson( world, "world.json" ) )
                {
                    g_DrawingLimitsInitialized = false;
                    s_LoadMsgTimer = 2.0f;
                }
            }
            if( s_SaveMsgTimer > 0.0f )
            {
                ImGui::SameLine();
                ImGui::TextColored( ImVec4( 0.2f, 1.0f, 0.2f, 1.0f ), "Saved" );
                s_SaveMsgTimer -= ImGui::GetIO().DeltaTime;
            }
            if( s_LoadMsgTimer > 0.0f )
            {
                ImGui::SameLine();
                ImGui::TextColored( ImVec4( 0.2f, 0.6f, 1.0f, 1.0f ), "Loaded" );
                s_LoadMsgTimer -= ImGui::GetIO().DeltaTime;
            }
        }
        else
        {
            ImGui::PushStyleVar( ImGuiStyleVar_Alpha, 0.95f );
            ImGui::BeginChild( "##BoundsPanel", ImVec2( 300, 0 ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
            ImGui::TextUnformatted( "Bounds" );
            ImGui::Separator();
            ImGui::InputFloat2( "X Min/Max", g_BoundsInputX, "%.3f" );
            ImGui::InputFloat2( "Y Min/Max", g_BoundsInputY, "%.3f" );

            bool differs = false;
            if( g_DrawingLimitsInitialized )
            {
                auto nearly = []( float a, float b )
                {
                    return std::fabs( a - b ) <= 1e-5f;
                };
                differs = !nearly( g_BoundsInputX[ 0 ], g_DrawingLimits.minBounds.x ) ||
                          !nearly( g_BoundsInputX[ 1 ], g_DrawingLimits.maxBounds.x ) ||
                          !nearly( g_BoundsInputY[ 0 ], g_DrawingLimits.minBounds.y ) ||
                          !nearly( g_BoundsInputY[ 1 ], g_DrawingLimits.maxBounds.y );
            }

            ImGui::BeginDisabled( !differs );
            if( ImGui::Button( "Apply" ) )
            {
                if( g_BoundsInputX[ 0 ] > g_BoundsInputX[ 1 ] ) std::swap( g_BoundsInputX[ 0 ], g_BoundsInputX[ 1 ] );
                if( g_BoundsInputY[ 0 ] > g_BoundsInputY[ 1 ] ) std::swap( g_BoundsInputY[ 0 ], g_BoundsInputY[ 1 ] );

                g_DrawingLimits.minBounds.x = g_BoundsInputX[ 0 ];
                g_DrawingLimits.maxBounds.x = g_BoundsInputX[  1 ];
                g_DrawingLimits.minBounds.y = g_BoundsInputY[ 0 ];
                g_DrawingLimits.maxBounds.y = g_BoundsInputY[ 1 ];

                g_DrawingLimits.range = g_DrawingLimits.maxBounds - g_DrawingLimits.minBounds;
                g_DrawingLimits.center = ( g_DrawingLimits.maxBounds + g_DrawingLimits.minBounds ) * 0.5f;
                g_DrawingLimits.scale = 2.0f / ( g_DrawingLimits.maxBounds - g_DrawingLimits.minBounds );
                g_DrawingLimits.scale.z = 1.0f;

                g_DrawingLimitsInitialized = true;
                g_BoundsEditorVisible = false;
            }
			ImGui::EndDisabled();
            ImGui::SameLine();
            if( ImGui::Button( "Cancel" ) )
            {
                if( g_DrawingLimitsInitialized )
                {
                    g_BoundsInputX[ 0 ] = g_DrawingLimits.minBounds.x;
                    g_BoundsInputX[ 1 ] = g_DrawingLimits.maxBounds.x;
                    g_BoundsInputY[ 0 ] = g_DrawingLimits.minBounds.y;
                    g_BoundsInputY[ 1 ] = g_DrawingLimits.maxBounds.y;
                }
                g_BoundsEditorVisible = false;
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        // overlays and interactions
        drawWaypointGuidSuffixOverlay( world, planner, pos, windowWidth, windowHeight );
        drawRoomNamesOverlay( world, planner, pos, windowWidth, windowHeight );

        // New: generic selection (when Waypoint Editor is inactive)
        handleEntitySelectionOnWorldView( pos, windowWidth, windowHeight );

        if( g_ShowWaypointEditorWindow )
        {
            handleWaypointEditorOnWorldView( pos, windowWidth, windowHeight );
            drawWaypointEditorOverlayOnWorldView( pos, windowWidth, windowHeight );
        }
    }
    ImGui::End();
}

void drawWaypointGuidSuffixOverlay( const gie::World& world, const gie::Planner& planner, ImVec2 pos, float windowWidth, float windowHeight )
{
    const gie::Blackboard* worldContext = &world.context();
    if( const auto* sim = planner.simulation( selectedSimulationGuid ) )
    {
        worldContext = &sim->context();
    }
    const auto* waypointSet = worldContext->entityTagRegister().tagSet( { "Waypoint" } );

    if( waypointSet && !waypointSet->empty() )
    {
        glm::vec3 offset = -g_DrawingLimits.center;
        glm::vec3 scale = g_DrawingLimits.scale;

        ImDrawList* dl = ImGui::GetWindowDrawList();

        for( auto waypointGuid : *waypointSet )
        {
            const bool isSelectedWaypoint = waypointGuid == g_WaypointEditSelectedGuid;

            if( !g_ShowWaypointGuidSuffix && !isSelectedWaypoint ) continue;

			const auto* e = world.entity( waypointGuid );
			if( !e ) continue;
            const auto* loc = e->property( "Location" );
            if( !loc ) continue;

            glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
            float x_px = ( p.x * 0.5f + 0.5f ) * windowWidth;
            float y_px = ( 1.0f - ( p.y * 0.5f + 0.5f ) ) * windowHeight;

            ImVec2 suffixPos{ pos.x + x_px, pos.y + y_px - 12.0f };

            unsigned long long id = static_cast< unsigned long long >( waypointGuid );
            unsigned int last4 = static_cast< unsigned int >( id % 10000ULL );
            char buf[8];
            snprintf( buf, sizeof( buf ), "%04u", last4 );

            const ImVec2 suffixSize = ImGui::CalcTextSize( buf );
            suffixPos.x -= suffixSize.x * 0.5f;
			suffixPos.y += isSelectedWaypoint ? 30.0f : 20.0f;

            ImGui::GetWindowDrawList()->AddText( ImVec2( suffixPos.x + 1, suffixPos.y + 1 ), IM_COL32( 0, 0, 0, 200 ), buf );
            ImGui::GetWindowDrawList()->AddText( suffixPos, IM_COL32( 255, 255, 0, 255 ), buf );

            std::string idxLabel = "wp?";
            auto nameHash = e->nameHash();
            if( nameHash != gie::InvalidStringHash )
            {
                std::string name( gie::stringRegister().get( nameHash ) );
                size_t j = name.size();
                if( name.find( "waypoint" ) == 0 )
                {
					while( j > 0 && std::isdigit( static_cast< unsigned char >( name[ j - 1 ] ) ) )
					{
						--j;
					}
					if( j < name.size() )
					{
						idxLabel = std::string( "wp" ) + name.substr( j );
					}
                }
                else
                {
					idxLabel = name;
                }
                        
            }

            const ImVec2 idxSize = ImGui::CalcTextSize( idxLabel.c_str() );
            const float idxPosYOffset = isSelectedWaypoint ? 35.0f : 15.0f;
			const ImVec2 idxPos{ pos.x + x_px - idxSize.x * 0.5f, suffixPos.y - ( idxSize.y + idxPosYOffset ) };

            ImGui::GetWindowDrawList()->AddText( ImVec2( idxPos.x + 1, idxPos.y + 1 ), IM_COL32( 0, 0, 0, 200 ), idxLabel.c_str() );
            ImGui::GetWindowDrawList()->AddText( idxPos, IM_COL32( 255, 255, 255, 255 ), idxLabel.c_str() );
        }
    }
}

void drawRoomNamesOverlay( const gie::World& world, const gie::Planner& planner, ImVec2 pos, float windowWidth, float windowHeight )
{
    const gie::Blackboard* context = &world.context();
    if( const auto* sim = planner.simulation( selectedSimulationGuid ) ) context = &sim->context();

    const auto* roomSet = context->entityTagRegister().tagSet( { gie::stringHasher( "Room" ) } );
    if( !roomSet || roomSet->empty() ) return;

    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = 2.0f / ( g_DrawingLimits.maxBounds - g_DrawingLimits.minBounds );

    ImDrawList* dl = ImGui::GetWindowDrawList();

    for( auto guid : *roomSet )
    {
        const auto* e = context->entity( guid ); if( !e ) { e = world.entity( guid ); if( !e ) continue; }
        auto disc = e->property( "Discovered" );
        if( !disc || !*disc->getBool() ) continue;
        auto disp = e->property( "DisplayName" );
        if( !disp || !*disp->getBool() ) continue;

        auto verticesPpt = e->property( "Vertices" ); if( !verticesPpt ) continue;
        const auto* verts = std::get_if< gie::Property::Vec3Vector >( &verticesPpt->value );
        if( !verts || verts->size() < 3 ) continue;

        float minXpx = std::numeric_limits<float>::max(), minYpx = std::numeric_limits<float>::max();
        for( const auto& v : *verts )
        {
            glm::vec3 p = ( v + offset ) * scale;
            float x_px = ( p.x * 0.5f + 0.5f ) * windowWidth;
            float y_px = ( 1.0f - ( p.y * 0.5f + 0.5f ) ) * windowHeight;
            if( x_px < minXpx ) minXpx = x_px;
            if( y_px < minYpx ) minYpx = y_px;
        }

        std::string name = "";
        if( e->nameHash() != gie::InvalidStringHash ) name = std::string( gie::stringRegister().get( e->nameHash() ) );
        if( name.empty() ) continue;

        ImVec2 textPos{ pos.x + minXpx + 4.0f, pos.y + minYpx + 4.0f };
        dl->AddText( ImVec2( textPos.x + 1, textPos.y + 1 ), IM_COL32( 0, 0, 0, 200 ), name.c_str() );
        dl->AddText( textPos, IM_COL32( 255, 255, 255, 255 ), name.c_str() );
    }
}

// Internal helpers copied from original file
static void handleWaypointEditorOnWorldView( ImVec2 pos, float windowWidth, float windowHeight )
{
    if( !g_ShowWaypointEditorWindow ) return;
    if( !g_WorldPtr ) return;

    ImGuiIO& io = ImGui::GetIO();
    const float localX = io.MousePos.x - pos.x;
    const float localY = io.MousePos.y - pos.y;
    const bool mouseOverWindow = ( localX >= 0.0f && localX <= windowWidth && localY >= 0.0f && localY <= windowHeight );

    auto MouseToWorld = [&]( float lx, float ly ) -> glm::vec3
    {
        float u = lx / windowWidth;
        float v = ly / windowHeight;
        float ndcX = u * 2.0f - 1.0f;
        float ndcY = ( 1.0f - v ) * 2.0f - 1.0f;
        glm::vec3 ndc{ ndcX, ndcY, 0.0f };
        return ndc / g_DrawingLimits.scale + g_DrawingLimits.center;
    };

    auto FindNearestWaypointUnderMouse = [&]() -> gie::Guid
    {
        const auto* set = g_WorldPtr->context().entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
        if( !set || set->empty() ) return gie::NullGuid;

        float bestDist2 = g_WaypointPickRadiusPx * g_WaypointPickRadiusPx;
        gie::Guid best = gie::NullGuid;
        glm::vec3 offset = -g_DrawingLimits.center;
        glm::vec3 scale = g_DrawingLimits.scale;
        for( auto guid : *set )
        {
            if( const auto* e = g_WorldPtr->entity( guid ) )
            {
                if( const auto* loc = e->property( "Location" ) )
                {
                    glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
                    float x_px = ( p.x * 0.5f + 0.5f ) * windowWidth;
                    float y_px = ( 1.0f - ( p.y * 0.5f + 0.5f ) ) * windowHeight;
                    float dx = x_px - localX;
                    float dy = y_px - localY;
                    float d2 = dx * dx + dy * dy;
                    if( d2 <= bestDist2 )
                    {
                        bestDist2 = d2;
                        best = guid;
                    }
                }
            }
        }
        return best;
    };

    if( !mouseOverWindow )
    {
        if( !ImGui::IsMouseDown( 0 ) )
        {
            g_WaypointDragActive = false;
            g_WaypointDragMoving = false;
        }
        return;
    }

    if( ImGui::IsMouseClicked( 0 ) )
    {
        gie::Guid nearest = FindNearestWaypointUnderMouse();
        if( nearest != gie::NullGuid )
        {
            g_WaypointEditSelectedGuid = nearest;
            g_SelectedEntityGuid = nearest; // sync selection to outliner
            g_WaypointEditPlaceArmed = false;
            if( auto e = g_WorldPtr->entity( g_WaypointEditSelectedGuid ) )
            {
                if( auto loc = e->property( "Location" ) )
                {
                    g_WaypointDragZ = loc->getVec3()->z;
                    g_WaypointDragActive = true;
                    g_WaypointDragMoving = false;
                    g_WaypointDragStartLocalX = localX;
                    g_WaypointDragStartLocalY = localY;
                }
            }
        }
    }

    if( g_WaypointDragActive && ImGui::IsMouseDown( 0 ) && g_WaypointEditSelectedGuid != gie::NullGuid )
    {
        if( !g_WaypointDragMoving )
        {
            float dx = localX - g_WaypointDragStartLocalX;
            float dy = localY - g_WaypointDragStartLocalY;
            float dist2 = dx * dx + dy * dy;
            if( dist2 >= g_WaypointPickRadiusPx * g_WaypointPickRadiusPx )
            {
                g_WaypointDragMoving = true;
            }
        }
        if( g_WaypointDragMoving )
        {
            glm::vec3 worldPos = MouseToWorld( localX, localY );
            if( auto e = g_WorldPtr->entity( g_WaypointEditSelectedGuid ) )
            {
                if( auto loc = e->property( "Location" ) )
                {
                    loc->value = glm::vec3{ worldPos.x, worldPos.y, g_WaypointDragZ };
                }
            }
        }
    }

    if( g_WaypointDragActive && ImGui::IsMouseReleased( 0 ) )
    {
        g_WaypointDragActive = false;
        g_WaypointDragMoving = false;
    }

    if( ImGui::IsMouseDoubleClicked( 0 ) )
    {
        gie::Guid nearest = FindNearestWaypointUnderMouse();
        if( nearest != gie::NullGuid )
        {
            g_WaypointEditSelectedGuid = nearest;
            g_SelectedEntityGuid = nearest; // sync selection to outliner
            g_WaypointEditPlaceArmed = true;
        }
    }

    if( g_WaypointEditPlaceArmed )
    {
        glm::vec3 wp = MouseToWorld( localX, localY );
        g_WaypointEditTargetWorldPos = wp;
        g_WaypointEditHasTargetWorldPos = true;
    }

    if( ImGui::IsMouseClicked( ImGuiMouseButton_Right ) && g_WaypointEditSelectedGuid != gie::NullGuid )
    {
        gie::Guid hit = FindNearestWaypointUnderMouse();
        if( hit != gie::NullGuid && hit != g_WaypointEditSelectedGuid )
        {
            if( auto selectedE = g_WorldPtr->entity( g_WaypointEditSelectedGuid ) )
            {
                auto linksPpt = selectedE->property( "Links" );
                if( linksPpt )
                {
                    if( auto arr = linksPpt->getGuidArray() )
                    {
                        auto it = std::find( arr->begin(), arr->end(), hit );
                        if( it != arr->end() )
                        {
                            arr->erase( it );
                        }
                    }
                }
            }
        }
    }
}

static void drawWaypointEditorOverlayOnWorldView( ImVec2 pos, float windowWidth, float windowHeight )
{
    if( !g_ShowWaypointEditorWindow ) return;
    if( !g_WorldPtr ) return;
    if( g_WaypointEditSelectedGuid == gie::NullGuid ) return;

    auto e = g_WorldPtr->entity( g_WaypointEditSelectedGuid );
    if( !e ) return;
    auto loc = e->property( "Location" );
    if( !loc ) return;

    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = g_DrawingLimits.scale;

    glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
    float x_px = ( p.x * 0.5f + 0.5f ) * windowWidth;
    float y_px = ( 1.0f - ( p.y * 0.5f + 0.5f ) ) * windowHeight;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c{ pos.x + x_px, pos.y + y_px };
    ImU32 col = IM_COL32( 255, 255, 0, 220 );
    ImU32 colBg = IM_COL32( 0, 0, 0, 120 );
    float r = g_WaypointPickRadiusPx;
    dl->AddCircleFilled( c, r + 2.0f, colBg, 24 );
    dl->AddCircle( c, r, col, 24, 2.0f );

    if( g_WaypointEditPlaceArmed )
    {
        dl->AddCircle( c, r * 0.6f, IM_COL32( 255, 120, 0, 220 ), 24, 2.0f );
    }
}
