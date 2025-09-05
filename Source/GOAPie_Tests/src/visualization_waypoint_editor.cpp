#include "visualization.h"

#include <algorithm>
#include <string>

void drawWaypointEditorWindow( gie::World& world, gie::Planner& planner )
{
    if( !g_ShowWaypointEditorWindow ) return;

    if( ImGui::Begin( "Waypoint Editor", &g_ShowWaypointEditorWindow ) )
    {
        if( g_IsLoading ) { DrawWindowLoadingOverlay(); ImGui::End(); return; }
        ImGui::TextUnformatted( "Single-click a waypoint to select it." );
        ImGui::SliderFloat( "Pick Radius (px)", &g_WaypointPickRadiusPx, 4.0f, 30.0f );
        if( g_WaypointEditSelectedGuid != gie::NullGuid )
        {
            auto e = world.entity( g_WaypointEditSelectedGuid );
            std::string name = e && e->nameHash() != gie::InvalidStringHash ? std::string( gie::stringRegister().get( e->nameHash() ) ) : std::string( "<unknown>" );
            ImGui::Text( "Selected: %s (%llu)", name.c_str(), static_cast< unsigned long long >( g_WaypointEditSelectedGuid ) );
            if( e )
            {
                if( auto loc = e->property( "Location" ) )
                {
                    glm::vec3 p = *loc->getVec3();
                    ImGui::Text( "Location: (%.2f, %.2f, %.2f)", p.x, p.y, p.z );
                }
            }
            if( ImGui::Button( "Clear Selection" ) )
            {
                ResetWaypointEditorState();
            }

            auto linksPpt = e->property( "Links" );
            gie::Property::GuidVector* selOutgoing = linksPpt ? linksPpt->getGuidArray() : nullptr; // don't auto-create

            std::set< gie::Guid > unionNeighbors;
            if( selOutgoing )
            {
                for( auto g : *selOutgoing ) if( g != g_WaypointEditSelectedGuid ) unionNeighbors.insert( g );
            }
            const auto* set = world.context().entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
            if( set )
            {
                for( auto g : *set )
                {
                    if( g == g_WaypointEditSelectedGuid ) continue;
                    auto n = world.entity( g );
                    if( !n ) continue;
                    auto lp = n->property( "Links" );
                    if( !lp ) continue; // don't auto-create
                    auto arr = lp->getGuidArray();
                    if( !arr ) continue;

                    for( auto gg : *arr ) if( gg == g_WaypointEditSelectedGuid ) unionNeighbors.insert( g );
                }
            }

            if( !unionNeighbors.empty() )
            {
                ImGui::Separator();
                ImGui::TextUnformatted( "Neighbors:" );
                for( auto g : unionNeighbors )
                {
                    auto n = world.entity( g );
                    std::string nm = n && n->nameHash() != gie::InvalidStringHash ? std::string( gie::stringRegister().get( n->nameHash() ) ) : std::string( "<unknown>" );
                    ImGui::BulletText( "%s (%llu)", nm.c_str(), static_cast< unsigned long long >( g ) );
                }
            }
        }
    }
    ImGui::End();
}

void ResetWaypointEditorState()
{
    g_WaypointEditSelectedGuid = gie::NullGuid;
    g_WaypointDragActive = false;
}

bool CancelWaypointEditorOngoingOperation()
{
    bool cancelled = false;
    if( g_WaypointDragActive ) { g_WaypointDragActive = false; g_WaypointDragMoving = false; cancelled = true; }
    // Do not clear selection here; only transient ops
    return cancelled;
}
