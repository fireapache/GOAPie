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
        ImGui::TextUnformatted( "Double-click a waypoint to arm repositioning, then click to place." );
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
            if( g_WaypointEditPlaceArmed )
            {
                ImGui::Text( "Move armed: Yes" );
                if( g_WaypointEditHasTargetWorldPos )
                {
                    ImGui::Text( "Target: (%.2f, %.2f, %.2f)", g_WaypointEditTargetWorldPos.x, g_WaypointEditTargetWorldPos.y, g_WaypointEditTargetWorldPos.z );
                }
                else
                {
                    ImGui::TextUnformatted( "Target: <move mouse over World View>" );
                }
            }
            else
            {
                ImGui::Text( "Move armed: No" );
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
                    if( std::find( arr->begin(), arr->end(), g_WaypointEditSelectedGuid ) != arr->end() )
                    {
                        unionNeighbors.insert( g );
                    }
                }
            }

            bool anyIncoming = false, anyOutgoing = false, anyBidirectional = false;
            for( auto neighborGuid : unionNeighbors )
            {
                auto neighbor = world.entity( neighborGuid ); if( !neighbor ) continue;
                auto neighborLinksPpt = neighbor->property( "Links" );
                auto neighborLinks = neighborLinksPpt ? neighborLinksPpt->getGuidArray() : nullptr; // don't auto-create
                bool hasOut = selOutgoing && std::find( selOutgoing->begin(), selOutgoing->end(), neighborGuid ) != selOutgoing->end();
                bool hasIn = neighborLinks && std::find( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ) != neighborLinks->end();
                anyOutgoing |= hasOut;
                anyIncoming |= hasIn;
                anyBidirectional |= ( hasOut && hasIn );
            }
            auto buttonAlpha = [&]( bool enabled ) { ImGui::PushStyleVar( ImGuiStyleVar_Alpha, enabled ? 1.0f : 0.4f ); };

            buttonAlpha( anyOutgoing );
            if( ImGui::Button( ">##clear_all_out" ) )
            {
                if( anyOutgoing )
                {
                    if( selOutgoing ) selOutgoing->clear();
                }
                else
                {
                    if( selOutgoing )
                    {
                        for( auto neighborGuid : unionNeighbors )
                        {
                            if( neighborGuid == g_WaypointEditSelectedGuid ) continue;
                            if( std::find( selOutgoing->begin(), selOutgoing->end(), neighborGuid ) == selOutgoing->end() )
                            {
                                selOutgoing->push_back( neighborGuid );
                            }
                        }
                    }
                }
            }
            ImGui::PopStyleVar();
            ImGui::SameLine();

            buttonAlpha( anyBidirectional );
            if( ImGui::Button( "-##clear_all_bi" ) )
            {
                for( auto neighborGuid : unionNeighbors )
                {
                    auto neighbor = world.entity( neighborGuid ); if( !neighbor ) continue;
                    auto neighborLinksPpt = neighbor->property( "Links" );
                    if( !neighborLinksPpt ) neighborLinksPpt = neighbor->createProperty( "Links", gie::Property::GuidVector{} );
                    auto neighborLinks = neighborLinksPpt->getGuidArray();
                    bool hasOut = selOutgoing && std::find( selOutgoing->begin(), selOutgoing->end(), neighborGuid ) != selOutgoing->end();
                    bool hasIn = neighborLinks && std::find( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ) != neighborLinks->end();
                    if( anyBidirectional )
                    {
                        if( hasOut && selOutgoing )
                        {
                            selOutgoing->erase( std::remove( selOutgoing->begin(), selOutgoing->end(), neighborGuid ), selOutgoing->end() );
                        }
                        if( hasIn && neighborLinks )
                        {
                            neighborLinks->erase( std::remove( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ), neighborLinks->end() );
                        }
                    }
                    else
                    {
                        if( selOutgoing )
                        {
                            if( std::find( selOutgoing->begin(), selOutgoing->end(), neighborGuid ) == selOutgoing->end() )
                                selOutgoing->push_back( neighborGuid );
                        }
                        if( neighborLinks )
                        {
                            if( std::find( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ) == neighborLinks->end() )
                                neighborLinks->push_back( g_WaypointEditSelectedGuid );
                        }
                    }
                }
            }
            ImGui::PopStyleVar();
            ImGui::SameLine();

            buttonAlpha( anyIncoming );
            if( ImGui::Button( "<##clear_all_in" ) )
            {
                for( auto neighborGuid : unionNeighbors )
                {
                    auto neighbor = world.entity( neighborGuid ); if( !neighbor ) continue;
                    auto neighborLinksPpt = neighbor->property( "Links" );
                    if( !neighborLinksPpt ) neighborLinksPpt = neighbor->createProperty( "Links", gie::Property::GuidVector{} );
                    auto neighborLinks = neighborLinksPpt->getGuidArray();
                    if( anyIncoming )
                    {
                        if( neighborLinks )
                        {
                            neighborLinks->erase( std::remove( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ), neighborLinks->end() );
                        }
                    }
                    else
                    {
                        if( neighborLinks )
                        {
                            if( std::find( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ) == neighborLinks->end() )
                                neighborLinks->push_back( g_WaypointEditSelectedGuid );
                        }
                    }
                }
            }
            ImGui::PopStyleVar();

            if( unionNeighbors.empty() )
            {
                ImGui::TextUnformatted( "No links." );
            }
            else
            {
                ImGui::Separator();
                ImGui::TextUnformatted( "Links:" );
                for( auto neighborGuid : unionNeighbors )
                {
                    auto neighbor = world.entity( neighborGuid );
                    if( !neighbor ) continue;
                    auto neighborLinksPpt = neighbor->property( "Links" );
                    if( !neighborLinksPpt ) neighborLinksPpt = neighbor->createProperty( "Links", gie::Property::GuidVector{} );
                    auto neighborLinks = neighborLinksPpt->getGuidArray();

                    bool hasOut = selOutgoing && std::find( selOutgoing->begin(), selOutgoing->end(), neighborGuid ) != selOutgoing->end();
                    bool hasIn = neighborLinks && std::find( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ) != neighborLinks->end();

                    auto buttonAlphaLocal = [&]( bool enabled )
                    {
                        ImGui::PushStyleVar( ImGuiStyleVar_Alpha, enabled ? 1.0f : 0.4f );
                    };

                    buttonAlphaLocal( hasIn );
                    std::string inLbl = std::string( "<##in_" ) + std::to_string( static_cast< int >( neighborGuid ) );
                    if( ImGui::Button( inLbl.c_str() ) )
                    {
                        if( neighborLinks )
                        {
                            auto it = std::find( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid );
                            if( it != neighborLinks->end() )
                            {
                                neighborLinks->erase( it );
                                hasIn = false;
                            }
                            else
                            {
                                neighborLinks->push_back( g_WaypointEditSelectedGuid );
                                hasIn = true;
                            }
                        }
                    }
                    ImGui::PopStyleVar();
                    ImGui::SameLine();

                    bool both = hasIn && hasOut;
                    buttonAlphaLocal( both );
                    std::string biLbl = std::string( "-##bi_" ) + std::to_string( static_cast< int >( neighborGuid ) );
                    if( ImGui::Button( biLbl.c_str() ) )
                    {
                        if( both )
                        {
                            if( neighborLinks )
                            {
                                neighborLinks->erase( std::remove( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ), neighborLinks->end() );
                            }
                            if( selOutgoing )
                            {
                                selOutgoing->erase( std::remove( selOutgoing->begin(), selOutgoing->end(), neighborGuid ), selOutgoing->end() );
                            }
                            hasIn = false; hasOut = false;
                        }
                        else
                        {
                            if( neighborLinks )
                            {
                                if( std::find( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ) == neighborLinks->end() )
                                    neighborLinks->push_back( g_WaypointEditSelectedGuid );
                            }
                            if( selOutgoing )
                            {
                                if( std::find( selOutgoing->begin(), selOutgoing->end(), neighborGuid ) == selOutgoing->end() )
                                    selOutgoing->push_back( neighborGuid );
                            }
                            hasIn = true; hasOut = true;
                        }
                    }
                    ImGui::PopStyleVar();
                    ImGui::SameLine();

                    buttonAlphaLocal( hasOut );
                    std::string outLbl = std::string( ">##out_" ) + std::to_string( static_cast< int >( neighborGuid ) );
                    if( ImGui::Button( outLbl.c_str() ) )
                    {
                        if( selOutgoing )
                        {
                            auto it = std::find( selOutgoing->begin(), selOutgoing->end(), neighborGuid );
                            if( it != selOutgoing->end() )
                            {
                                selOutgoing->erase( it );
                                hasOut = false;
                            }
                            else
                            {
                                selOutgoing->push_back( neighborGuid );
                                hasOut = true;
                            }
                        }
                    }
                    ImGui::PopStyleVar();
                    ImGui::SameLine( 0.0f, 8.0f );

                    std::string nName = neighbor->nameHash() != gie::InvalidStringHash ? std::string( gie::stringRegister().get( neighbor->nameHash() ) ) : std::string( "<unnamed>" );
                    ImGui::Text( "%s (%llu)", nName.c_str(), static_cast< unsigned long long >( neighborGuid ) );
                }
            }
        }
        else
        {
            ImGui::TextUnformatted( "Selected: <none>" );
        }
        ImGui::Separator();
        ImGui::TextUnformatted( g_ShowWorldViewWindow ? "World View is open." : "Open World View to interact." );
    }
    ImGui::End();
}

void ResetWaypointEditorState()
{
    g_WaypointEditSelectedGuid = gie::NullGuid;
    g_WaypointEditPlaceArmed = false;
    g_WaypointEditHasTargetWorldPos = false;
    g_WaypointDragActive = false;
}

bool CancelWaypointEditorOngoingOperation()
{
    bool cancelled = false;
    if( g_WaypointEditPlaceArmed ) { g_WaypointEditPlaceArmed = false; cancelled = true; }
    if( g_WaypointDragActive ) { g_WaypointDragActive = false; g_WaypointDragMoving = false; cancelled = true; }
    // Do not clear selection here; only transient ops
    return cancelled;
}
