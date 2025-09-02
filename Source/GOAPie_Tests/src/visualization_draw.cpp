#include "visualization.h"

#include <algorithm>
#include <string>

static bool isRoomName( std::string_view name )
{
    return name == "Garage" || name == "Kitchen" || name == "Corridor" || name == "LivingRoom" || name == "Bathroom" || name == "BedroomA" || name == "BedroomB";
}

static void drawLinks(
    const gie::Entity* waypointEntity,
    const gie::World& world,
    const glm::vec3& offset,
    const glm::vec3& scale,
    const glm::vec3& scaledLocation,
    bool isSelectedWaypoint )
{
    if( auto linksPpt = waypointEntity->property( "Links" ) )
    {
        auto linkedGuids = linksPpt->getGuidArray();
        if( linkedGuids )
        {
            if( isSelectedWaypoint ) glColor3f( 0.85f, 0.85f, 0.85f );
            else                     glColor3f( 0.5f,  0.5f,  0.5f  );
            for( const auto& linkedGuid : *linkedGuids )
            {
                auto linkedEntity = world.entity( linkedGuid );
                if( auto linkedLocationPpt = linkedEntity->property( "Location" ) )
                {
                    glm::vec3 linkedLocation = *linkedLocationPpt->getVec3();
                    glm::vec3 scaledLinkedLocation = ( linkedLocation + offset ) * scale;

                    glBegin( GL_LINES );
                    glVertex3f( scaledLocation.x, scaledLocation.y, scaledLocation.z );
                    glVertex3f( scaledLinkedLocation.x, scaledLinkedLocation.y, scaledLinkedLocation.z );
                    glEnd();

                    if( g_ShowWaypointArrows || isSelectedWaypoint )
                    {
                        glm::vec2 start2{ scaledLocation.x, scaledLocation.y };
                        glm::vec2 end2{ scaledLinkedLocation.x, scaledLinkedLocation.y };
                        glm::vec2 dir = end2 - start2;
                        float len = glm::length( dir );
                        if( len > 1e-6f )
                        {
                            dir /= len;
                            glm::vec2 perp{ -dir.y, dir.x };

                            const float arrowLength = 0.03f;
                            const float arrowWidth = 0.02f;

                            glm::vec2 base = end2 - dir * arrowLength;
                            glm::vec2 left = base + perp * ( arrowWidth * 0.5f );
                            glm::vec2 right = base - perp * ( arrowWidth * 0.5f );
                            float zTip = scaledLinkedLocation.z;
                            glBegin( GL_TRIANGLES );
                            glVertex3f( end2.x, end2.y, zTip );
                            glVertex3f( left.x, left.y, zTip );
                            glVertex3f( right.x, right.y, zTip );
                            glEnd();

                            if( isSelectedWaypoint )
                            {
                                bool hasReverse = false;
                                if( auto neighborLinksPpt = linkedEntity->property( "Links" ) )
                                {
                                    if( auto neighborLinks = neighborLinksPpt->getGuidArray() )
                                    {
                                        hasReverse = std::find( neighborLinks->begin(), neighborLinks->end(), waypointEntity->guid() ) != neighborLinks->end();
                                    }
                                }
                                if( hasReverse )
                                {
                                    glm::vec2 dirRev = -dir;
                                    glm::vec2 baseS = start2 - dirRev * arrowLength;
                                    glm::vec2 leftS = baseS + perp * ( arrowWidth * 0.5f );
                                    glm::vec2 rightS = baseS - perp * ( arrowWidth * 0.5f );
                                    float zStart = scaledLocation.z;
                                    glBegin( GL_TRIANGLES );
                                    glVertex3f( start2.x, start2.y, zStart );
                                    glVertex3f( leftS.x, leftS.y, zStart );
                                    glVertex3f( rightS.x, rightS.y, zStart );
                                    glEnd();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void drawWaypointsAndLinks( const gie::World& world, const gie::Planner& planner )
{
    const gie::TagSet* waypointGuids = nullptr;
    const gie::Blackboard* context = &world.context();

    const gie::Simulation* selectedSimulation = planner.simulation( selectedSimulationGuid );
    if( selectedSimulation )
    {
        context = &selectedSimulation->context();
    }

    waypointGuids = context->entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
    if( !waypointGuids || waypointGuids->empty() )
    {
        return;
    }

    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = g_DrawingLimits.scale;

    glPointSize( 10.0f );
    glColor3f( 1.0f, 1.0f, 1.0f );

    for( gie::Guid waypointGuid : *waypointGuids )
    {
        auto waypointEntity = world.entity( waypointGuid );
        if( auto locationPpt = waypointEntity->property( "Location" ) )
        {
            glm::vec3 location = *locationPpt->getVec3();
            glm::vec3 scaledLocation = ( location + offset ) * scale;

            glBegin( GL_POINTS );
            glVertex3f( scaledLocation.x, scaledLocation.y, scaledLocation.z );
            glEnd();

            bool isSelected = ( g_WaypointEditSelectedGuid != gie::NullGuid && waypointGuid == g_WaypointEditSelectedGuid );
            drawLinks( waypointEntity, world, offset, scale, scaledLocation, isSelected );
        }
    }
}

void drawHeistOverlays( const gie::World& world, const gie::Planner& planner )
{
    const gie::Blackboard* context = &world.context();
    const gie::Simulation* selectedSimulation = planner.simulation( selectedSimulationGuid );
    if( selectedSimulation ) context = &selectedSimulation->context();

    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = g_DrawingLimits.scale;

    const auto* waypointSet = context->entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
    if( waypointSet && !waypointSet->empty() )
    {
        const float halfW = 4.0f, halfH = 3.0f;
        glm::vec3 halfWorld{ halfW, halfH, 0.0f };
        glm::vec3 half = halfWorld * scale;
        glColor3f( 0.2f, 0.7f, 0.9f );
        for( auto guid : *waypointSet )
        {
            auto e = world.entity( guid ); if( !e ) continue;
            auto nm = gie::stringRegister().get( e->nameHash() );
            if( !isRoomName( nm ) ) continue;
            auto loc = e->property( "Location" ); if( !loc ) continue;
            glm::vec3 c = ( *loc->getVec3() + offset ) * scale;
            glBegin( GL_LINE_LOOP );
            glVertex3f( c.x - half.x, c.y - half.y, c.z );
            glVertex3f( c.x + half.x, c.y - half.y, c.z );
            glVertex3f( c.x + half.x, c.y + half.y, c.z );
            glVertex3f( c.x - half.x, c.y + half.y, c.z );
            glEnd();
        }
    }

    const auto* connectorSet = context->entityTagRegister().tagSet( { gie::stringHasher( "Connector" ) } );
    if( connectorSet && !connectorSet->empty() )
    {
        bool alarmArmed = false;
        for( const auto& [ eg, ent ] : context->entities() )
        {
            if( gie::stringRegister().get( ent.nameHash() ) == std::string_view( "AlarmSystem" ) )
            {
                if( auto a = context->entity( eg ) )
                {
                    if( auto p = a->property( "Armed" ) ) alarmArmed = *p->getBool();
                }
                break;
            }
        }

        glLineWidth( 2.0f );
        for( auto guid : *connectorSet )
        {
            auto c = context->entity( guid ); if( !c ) continue;
            bool locked = *c->property( "Locked" )->getBool();
            bool blocked = *c->property( "Blocked" )->getBool();
            bool barred = *c->property( "Barred" )->getBool();
            bool alarmed = *c->property( "Alarmed" )->getBool();
            bool blockedAny = locked || blocked || barred;

            if( blockedAny ) glColor3f( 1.0f, 0.2f, 0.2f );
            else if( alarmArmed && alarmed ) glColor3f( 1.0f, 1.0f, 0.2f );
            else glColor3f( 0.2f, 1.0f, 0.2f );

            auto fromG = *c->property( "From" )->getGuid();
            auto toG   = *c->property( "To" )->getGuid();
            auto fromE = context->entity( fromG ); if( !fromE ) fromE = world.entity( fromG );
            auto toE   = context->entity( toG );   if( !toE )   toE   = world.entity( toG );
            if( fromE && toE )
            {
                auto l0 = fromE->property( "Location" );
                auto l1 = toE->property( "Location" );
                if( l0 && l1 )
                {
                    glm::vec3 a = ( *l0->getVec3() + offset ) * scale;
                    glm::vec3 b = ( *l1->getVec3() + offset ) * scale;
                    glBegin( GL_LINES );
                    glVertex3f( a.x, a.y, a.z );
                    glVertex3f( b.x, b.y, b.z );
                    glEnd();
                }
            }

            if( auto loc = c->property( "Location" ) )
            {
                glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
                const float s = 0.02f;
                glBegin( GL_LINES );
                glVertex3f( p.x - s, p.y, p.z ); glVertex3f( p.x + s, p.y, p.z );
                glVertex3f( p.x, p.y - s, p.z ); glVertex3f( p.x, p.y + s, p.z );
                glEnd();
            }
        }
        glLineWidth( 1.0f );
    }

    gie::Entity const* alarmPanel = nullptr;
    gie::Entity const* fuseBox = nullptr;
    gie::Entity const* safe = nullptr;
    for( const auto& [ eg, ent ] : context->entities() )
    {
        auto name = gie::stringRegister().get( ent.nameHash() );
        if( name == "AlarmPanelEntity" ) alarmPanel = context->entity( eg );
        else if( name == "FuseBoxEntity" ) fuseBox = context->entity( eg );
        else if( name == "Safe" ) safe = context->entity( eg );
    }

    auto drawFilledQuad = []( const glm::vec3& c, float s )
    {
        glBegin( GL_QUADS );
        glVertex3f( c.x - s, c.y - s, c.z );
        glVertex3f( c.x + s, c.y - s, c.z );
        glVertex3f( c.x + s, c.y + s, c.z );
        glVertex3f( c.x - s, c.y + s, c.z );
        glEnd();
    };
    auto drawDiamond = []( const glm::vec3& c, float s )
    {
        glBegin( GL_TRIANGLES );
        glVertex3f( c.x, c.y - s, c.z ); glVertex3f( c.x - s, c.y, c.z ); glVertex3f( c.x + s, c.y, c.z );
        glVertex3f( c.x, c.y + s, c.z ); glVertex3f( c.x - s, c.y, c.z ); glVertex3f( c.x + s, c.y, c.z );
        glEnd();
    };

    const float poiSize = 0.02f;

    if( alarmPanel )
    {
        if( auto loc = alarmPanel->property( "Location" ) )
        {
            glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
            glColor3f( 0.2f, 0.6f, 1.0f );
            drawFilledQuad( p, poiSize );
        }
    }
    if( fuseBox )
    {
        if( auto loc = fuseBox->property( "Location" ) )
        {
            glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
            glColor3f( 1.0f, 0.6f, 0.2f );
            drawFilledQuad( p, poiSize );
        }
    }
    if( safe )
    {
        auto roomG = *const_cast< gie::Entity* >( safe )->property( "InRoom" )->getGuid();
        const auto* room = context->entity( roomG ); if( !room ) room = world.entity( roomG );
        if( room )
        {
            if( auto loc = room->property( "Location" ) )
            {
                glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
                glColor3f( 1.0f, 0.2f, 1.0f );
                drawDiamond( p, poiSize * 1.2f );
            }
        }
    }
}

void drawTrees( const gie::World& world, const gie::Planner& planner )
{
    const gie::TagSet* treeGuids = nullptr;
    const gie::Blackboard* context = &world.context();

    const gie::Simulation* selectedSimulation = planner.simulation( selectedSimulationGuid );
    if( selectedSimulation )
    {
        context = &selectedSimulation->context();
    }

    treeGuids = context->entityTagRegister().tagSet( { gie::stringHasher( "Tree" ) } );
    if( !treeGuids || treeGuids->empty() )
    {
        return;
    }

    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = g_DrawingLimits.scale;

    auto treeUpTag = gie::stringHasher( "TreeUp" );
    auto treeDownTag = gie::stringHasher( "TreeDown" );

    glPointSize( 10.0f );

    for( gie::Guid treeGuid : *treeGuids )
    {
        auto treeEntity = context->entity( treeGuid );
        if( auto locationPpt = treeEntity->property( "Location" ) )
        {
            glm::vec3 location = *locationPpt->getVec3();
            glm::vec3 scaledLocation = ( location + offset ) * scale;

            if( treeEntity->hasTag( treeUpTag ) )
            {
                glColor3f( 0.0f, 1.0f, 0.0f );
            }
            else
            {
                glColor3f( 1.0f, 0.0f, 0.0f );
            }

            glBegin( GL_POINTS );
            glVertex3f( scaledLocation.x, scaledLocation.y, scaledLocation.z );
            glEnd();
        }
    }
}

void drawSelectedSimulationPath( const gie::World& world, const gie::Planner& planner )
{
    const gie::Simulation* selectedSimulation = planner.simulation( selectedSimulationGuid );
    if( !selectedSimulation ) return;

    const gie::Blackboard* contextBB = &selectedSimulation->context();

    glm::vec3 minB = g_DrawingLimits.minBounds;
    glm::vec3 maxB = g_DrawingLimits.maxBounds;
    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = 2.0f / ( maxB - minB );

    const auto* openedArg = selectedSimulation->arguments().get( "PF_Opened" );
    const auto* visitedArg = selectedSimulation->arguments().get( "PF_Visited" );
    const auto* backsArg = selectedSimulation->arguments().get( "PF_Backtracks" );
    const auto* openedOffsetsArg = selectedSimulation->arguments().get( "PF_OpenedOffsets" );
    const auto* visitedOffsetsArg = selectedSimulation->arguments().get( "PF_VisitedOffsets" );
    const auto* backOffsetsArg = selectedSimulation->arguments().get( "PF_BacktrackOffsets" );

    bool hasStepData = openedArg && visitedArg && backsArg && openedOffsetsArg && visitedOffsetsArg && backOffsetsArg;

    if( hasStepData && g_PathStepMode && g_PathStepSimGuid == selectedSimulation->guid() )
    {
        const auto& openedAll = std::get< gie::Property::GuidVector >( *openedArg );
        const auto& visitedAll = std::get< gie::Property::GuidVector >( *visitedArg );
        const auto& backAll = std::get< gie::Property::GuidVector >( *backsArg );
        const auto& openedOff = std::get< gie::Property::IntegerVector >( *openedOffsetsArg );
        const auto& visitedOff = std::get< gie::Property::IntegerVector >( *visitedOffsetsArg );
        const auto& backOff = std::get< gie::Property::IntegerVector >( *backOffsetsArg );
        int statesCount = static_cast<int>( openedOff.size() > 0 ? openedOff.size() - 1 : 0 );
        if( statesCount <= 0 ) return;
        int s = std::min( std::max( g_PathStepIndex, 0 ), statesCount - 1 );

        int o0 = openedOff[ s ];
        int o1 = openedOff[ s + 1 ];
        int v0 = visitedOff[ s ];
        int v1 = visitedOff[ s + 1 ];
        int b0 = backOff[ s ];
        int b1 = backOff[ s + 1 ];

        glPointSize( 9.0f );
        glColor3f( 1.0f, 1.0f, 0.0f );
        glBegin( GL_POINTS );
        for( int i = o0; i < o1; ++i )
        {
            auto e = world.entity( openedAll[ i ] );
            if( !e ) continue;
            auto loc = e->property( "Location" );
            if( !loc ) continue;
            glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
            glVertex3f( p.x, p.y, p.z );
        }
        glEnd();

        glPointSize( 11.0f );
        glColor3f( 0.0f, 1.0f, 1.0f );
        glBegin( GL_POINTS );
        for( int i = v0; i < v1; ++i )
        {
            auto e = world.entity( visitedAll[ i ] );
            if( !e ) continue;
            auto loc = e->property( "Location" );
            if( !loc ) continue;
            glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
            glVertex3f( p.x, p.y, p.z );
        }
        glEnd();

        auto drawArrow = []( const glm::vec3& a, const glm::vec3& b )
        {
            glm::vec2 a2{ a.x, a.y };
            glm::vec2 b2{ b.x, b.y };
            glm::vec2 dir = b2 - a2;
            float len = glm::length( dir );
            if( len <= 1e-6f ) return;
            dir /= len;
            glm::vec2 perp{ -dir.y, dir.x };

            const float arrowLen = 0.03f;
            const float arrowWidth = 0.02f;
            glm::vec2 mid = ( a2 + b2 ) * 0.5f;
            glm::vec2 tip = mid + dir * arrowLen * 0.5f;
            glm::vec2 base = mid - dir * arrowLen * 0.5f;
            glm::vec2 left = base + perp * ( arrowWidth * 0.5f );
            glm::vec2 right = base - perp * ( arrowWidth * 0.5f );

            float z = ( a.z + b.z ) * 0.5f;
            glBegin( GL_TRIANGLES );
            glVertex3f( tip.x, tip.y, z );
            glVertex3f( left.x, left.y, z );
            glVertex3f( right.x, right.y, z );
            glEnd();
        };

        glColor3f( 1.0f, 1.0f, 1.0f );
        for( int i = b0; i + 1 < b1; i += 2 )
        {
            auto node = world.entity( backAll[ i ] );
            auto prev = world.entity( backAll[ i + 1 ] );
            if( !node || !prev ) continue;
            auto nLoc = node->property( "Location" );
            auto pLoc = prev->property( "Location" );
            if( !nLoc || !pLoc ) continue;
            glm::vec3 a = ( *pLoc->getVec3() + offset ) * scale;
            glm::vec3 b = ( *nLoc->getVec3() + offset ) * scale;
            glBegin( GL_LINES );
            glVertex3f( a.x, a.y, a.z );
            glVertex3f( b.x, b.y, b.z );
            glEnd();
            drawArrow( a, b );
        }

        return;
    }

    const auto* pathArg = selectedSimulation->arguments().get( "PathToTarget" );
    if( !pathArg ) return;

    const auto& pathGuids = std::get< std::vector< gie::Guid > >( *pathArg );
    if( pathGuids.size() <= 1 ) return;

    glLineWidth( 3.0f );
    glColor3f( 1.0f, 0.0f, 1.0f );
    glBegin( GL_LINE_STRIP );
    for( auto guid : pathGuids )
    {
        auto e = world.entity( guid );
        if( auto loc = e->property( "Location" ) )
        {
            glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
            glVertex3f( p.x, p.y, p.z );
        }
    }
    glEnd();

    auto drawArrowAtMid = []( const glm::vec3& a, const glm::vec3& b )
    {
        glm::vec2 a2{ a.x, a.y };
        glm::vec2 b2{ b.x, b.y };
        glm::vec2 dir = b2 - a2;
        float len = glm::length( dir );
        if( len <= 1e-6f ) return;
        dir /= len;
        glm::vec2 perp{ -dir.y, dir.x };

        const float arrowLen = 0.03f;
        const float arrowWidth = 0.025f;
        glm::vec2 mid = ( a2 + b2 ) * 0.5f;
        glm::vec2 tip = mid + dir * arrowLen * 0.5f;
        glm::vec2 base = mid - dir * arrowLen * 0.5f;
        glm::vec2 left = base + perp * ( arrowWidth * 0.5f );
        glm::vec2 right = base - perp * ( arrowWidth * 0.5f );

        float z = ( a.z + b.z ) * 0.5f;
        glBegin( GL_TRIANGLES );
        glVertex3f( tip.x, tip.y, z );
        glVertex3f( left.x, left.y, z );
        glVertex3f( right.x, right.y, z );
        glEnd();
    };

    glColor3f( 1.0f, 0.0f, 1.0f );
    for( size_t i = 1; i < pathGuids.size(); ++i )
    {
        auto e0 = world.entity( pathGuids[ i - 1 ] );
        auto e1 = world.entity( pathGuids[ i ] );
        if( !e0 || !e1 ) continue;
        auto l0 = e0->property( "Location" );
        auto l1 = e1->property( "Location" );
        if( !l0 || !l1 ) continue;
        glm::vec3 a = ( *l0->getVec3() + offset ) * scale;
        glm::vec3 b = ( *l1->getVec3() + offset ) * scale;
        drawArrowAtMid( a, b );
    }

    const auto agentStartLocationArgument = selectedSimulation->arguments().get( "AgentStartLocation" );
    if( !pathGuids.empty() && agentStartLocationArgument )
    {
        auto agentStartLocPpt = std::get< glm::vec3 >( *agentStartLocationArgument );
        auto firstWp = world.entity( pathGuids.front() );
        if( auto wpLoc = firstWp->property( "Location" ) )
        {
            glm::vec3 a = ( agentStartLocPpt + offset ) * scale;
            glm::vec3 b = ( *wpLoc->getVec3() + offset ) * scale;
            glBegin( GL_LINES );
            glVertex3f( a.x, a.y, a.z );
            glVertex3f( b.x, b.y, b.z );
            glEnd();

            drawArrowAtMid( a, b );
        }
    }

    const auto* targetArg = selectedSimulation->arguments().get( "PathTarget" );
    if( targetArg && !pathGuids.empty() )
    {
        gie::Guid targetGuid = std::get< gie::Guid >( *targetArg );
        auto lastWp = world.entity( pathGuids.back() );
        auto targetEntity = contextBB->entity( targetGuid );
        if( lastWp && targetEntity )
        {
            auto lastLoc = lastWp->property( "Location" );
            auto targetLoc = targetEntity->property( "Location" );
            if( lastLoc && targetLoc )
            {
                glm::vec3 a = ( *lastLoc->getVec3() + offset ) * scale;
                glm::vec3 b = ( *targetLoc->getVec3() + offset ) * scale;
                glBegin( GL_LINES );
                glVertex3f( a.x, a.y, a.z );
                glVertex3f( b.x, b.y, b.z );
                glEnd();

                drawArrowAtMid( a, b );
            }
        }
    }

    glLineWidth( 1.0f );
}

void drawAgentCrosshair( const gie::World& world, const gie::Planner& planner )
{
    const gie::Simulation* selectedSimulation = planner.simulation( selectedSimulationGuid );
    if( !selectedSimulation ) return;

    const gie::Blackboard* contextBB = &selectedSimulation->context();

    glm::vec3 minB = g_DrawingLimits.minBounds;
    glm::vec3 maxB = g_DrawingLimits.maxBounds;
    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = 2.0f / ( maxB - minB );

    auto agentEntity = contextBB->entity( planner.agent()->guid() );
    if( !agentEntity ) return;
    auto agentLocPpt = agentEntity->property( "Location" );
    if( !agentLocPpt ) return;

    glm::vec3 p = ( *agentLocPpt->getVec3() + offset ) * scale;

    glColor3f( 1.0f, 0.5f, 0.0f );
    const float half = 0.03f;
    glLineWidth( 2.0f );
    glBegin( GL_LINES );
    glVertex3f( p.x - half, p.y, p.z );
    glVertex3f( p.x + half, p.y, p.z );
    glVertex3f( p.x, p.y - half, p.z );
    glVertex3f( p.x, p.y + half, p.z );
    glEnd();
    glLineWidth( 1.0f );
}

void drawDiscoveredRoomsWalls( const gie::World& world, const gie::Planner& planner )
{
    const gie::Blackboard* context = &world.context();
    if( const auto* sim = planner.simulation( selectedSimulationGuid ) ) context = &sim->context();

    const auto* roomSet = context->entityTagRegister().tagSet( { gie::stringHasher( "Room" ) } );
    if( !roomSet || roomSet->empty() ) return;

    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = 2.0f / ( g_DrawingLimits.maxBounds - g_DrawingLimits.minBounds );
    scale.z = 1.0f;

    glColor3f( 0.85f, 0.85f, 0.85f );
    glLineWidth( 2.0f );

    for( auto guid : *roomSet )
    {
        const auto* e = context->entity( guid ); if( !e ) { e = world.entity( guid ); if( !e ) continue; }
        auto disc = e->property( "Discovered" );
        if( !disc || !*disc->getBool() ) continue;

        auto verticesPpt = e->property( "Vertices" );
        if( !verticesPpt ) continue;
        const auto* verts = std::get_if< gie::Property::Vec3Vector >( &verticesPpt->value );
        if( !verts || verts->size() < 3 ) continue;

        glBegin( GL_LINES );
        for( size_t i = 0; i < verts->size(); ++i )
        {
            const auto& currentVertex = (*verts)[i];
            glm::vec3 currentPos = ( currentVertex + offset ) * scale;
            size_t nextIndex = ( i + 1 ) % verts->size();
            const auto& nextVertex = (*verts)[nextIndex];
            glm::vec3 nextPos = ( nextVertex + offset ) * scale;
            glVertex3f( currentPos.x, currentPos.y, currentPos.z );
            glVertex3f( nextPos.x, nextPos.y, nextPos.z );
        }
        glEnd();
    }

    glLineWidth( 1.0f );
}

void updateDrawingBounds( const gie::World& world )
{
    if( g_DrawingLimitsInitialized ) return;

    const gie::Blackboard* worldContext = &world.context();

    glm::vec3 minBounds( std::numeric_limits< float >::max() );
    glm::vec3 maxBounds( std::numeric_limits< float >::lowest() );

    const auto* drawTagSet = worldContext->entityTagRegister().tagSet( { "Draw" } );
    const auto* waypointsTagSet = worldContext->entityTagRegister().tagSet( { "Waypoint" } );

    const std::set< gie::Guid >* setToUse = drawTagSet && !drawTagSet->empty() ? drawTagSet
                                             : ( waypointsTagSet && !waypointsTagSet->empty() ? waypointsTagSet : nullptr );
    if( !setToUse )
    {
        return;
    }

    for( auto guid : *setToUse )
    {
        if( const auto* e = world.entity( guid ) )
        {
            if( const auto* loc = e->property( "Location" ) )
            {
                glm::vec3 p = *loc->getVec3();
                minBounds = glm::min( minBounds, p );
                maxBounds = glm::max( maxBounds, p );
            }
            if( const auto* verticesPpt = e->property( "Vertices" ) )
            {
                const auto* verts = std::get_if< gie::Property::Vec3Vector >( &verticesPpt->value );
                if( verts && !verts->empty() )
                {
                    for( const auto& vertex : *verts )
                    {
                        minBounds = glm::min( minBounds, vertex );
                        maxBounds = glm::max( maxBounds, vertex );
                    }
                }
            }
        }
    }

    glm::vec3 range = maxBounds - minBounds;
    glm::vec3 center = ( maxBounds + minBounds ) * 0.5f;

    minBounds = minBounds - range * g_DrawingLimits.margin;
    maxBounds = maxBounds + range * g_DrawingLimits.margin;

    glm::vec3 scale = 2.0f / ( maxBounds - minBounds );
    scale.z = 1.0f;

    g_DrawingLimits.minBounds = minBounds;
    g_DrawingLimits.maxBounds = maxBounds;
    g_DrawingLimits.center = center;
    g_DrawingLimits.range = range;
    g_DrawingLimits.scale = scale;

    g_DrawingLimitsInitialized = true;
}
