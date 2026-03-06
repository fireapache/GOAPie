#include <functional>
#include <array>
#include <algorithm>
#include <random>

#include <goapie.h>

#include "example.h"
#include "waypoint_navigation.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

// Heist Example (Example 6)
// This sample sets up a small house layout and simulates multiple routes to open a safe.
// It demonstrates:
//  - Multiple entry routes (front door, back door, kitchen window)
//  - Tools and keys gating (crowbar, bolt cutters, lockpick, keys, stethoscope)
//  - System interaction (alarm panel, fuse box)
//  - GOAP action simulators with heuristic guidance and rich debug messages

const char* heistOpenSafeDescription()
{
    return "Complex heist scenario: plan actions to open a safe using tools, keys, or brute force.";
}

// Simple helpers
static inline float clampf( float v, float lo, float hi ) { return std::max( lo, std::min( v, hi ) ); }

using gie::Guid;

// Common string hashes
static inline gie::StringHash H( const char* s ) { return gie::stringHasher( s ); }

// UI + world knobs for the heist example
struct HeistToggles
{
    // Entry points — kitchen window is the only free entry, others require tools
    bool frontDoorLocked{ true };
    bool frontDoorAlarmed{ true };
    bool backDoorLocked{ true };
    bool backDoorBlocked{ false };
    bool kitchenWindowLocked{ false };
    bool kitchenWindowBarred{ false };

    // Systems — alarm creates additional complexity
    bool alarmArmed{ true };

    // Items — tools scattered around the property
    bool placeCrowbar{ true };
    bool placeBoltCutter{ true };
    bool placeLockpick{ true };
    bool placeStethoscope{ false };
    bool placeFrontKey{ false };
    bool placeBackKey{ false };
    bool placeSafeKey{ false };

    // Safe — code-lock requires collecting code pieces from around the house
    int safeRoomIndex{ 0 }; // 0: BedroomA, 1: BedroomB
    int safeLockMode{ 1 };  // 0: Key, 1: Code, 2: Heavy (requires crack)
    int requiredCodePieces{ 2 };

    // heuristic weights
    float travelCostWeight{ 1.0f };
    float lockedPenalty{ 10.0f };
    float barredPenalty{ 12.0f };
    float alarmedPenalty{ 8.0f };
};

// Global toggles are kept static inside this TU (edited via ImGui).
static HeistToggles g_Toggles{};

 // Forward declarations
static void ImGuiFunc6( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimulationGuid );
static void ResetHeistWorldFromToggles( gie::World& world );
static gie::Entity* FindRoom( gie::World& world, const char* roomName );
static void populateWorld( ExampleParameters& params, gie::Agent* agent );

 // Split example 6 setup into world-only and actions-only functions.
 // heistOpenSafe_world builds the World/Agent/Goal and returns the created agent pointer.
 // heistOpenSafe_actions registers native action simulators and action set entries.
 gie::Agent* heistOpenSafe_world( ExampleParameters& params );
 static int heistOpenSafe_actions( ExampleParameters& params, gie::Agent* agent );

// Helper to get all entities tagged with "Access"
static std::vector<gie::Guid> getAccessPoints( const gie::Blackboard& ctx )
{
    std::vector<gie::Guid> accessPoints;
    if( auto accessSet = ctx.entityTagRegister().tagSet( "Access" ) )
    {
        accessPoints.assign( accessSet->begin(), accessSet->end() );
    }
    return accessPoints;
}

// Find an entity by name, searching the entire blackboard hierarchy.
// Simulation contexts only store locally modified copies — iteration over entities()
// misses unmodified entities living in parent blackboards. This helper walks the
// parent chain so entity lookups by name work correctly during planning.
static gie::Entity* FindEntityByName( gie::Blackboard& ctx, const char* name )
{
    for( const auto& kv : ctx.entities() )
        if( gie::stringRegister().get( kv.second.nameHash() ) == name )
            return ctx.entity( kv.first );
    const gie::Blackboard* p = ctx.parent();
    while( p )
    {
        for( const auto& kv : p->entities() )
            if( gie::stringRegister().get( kv.second.nameHash() ) == name )
                return ctx.entity( kv.first );
        p = p->parent();
    }
    return nullptr;
}

static const gie::Entity* FindEntityByName( const gie::Blackboard& ctx, const char* name )
{
    for( const auto& kv : ctx.entities() )
        if( gie::stringRegister().get( kv.second.nameHash() ) == name )
            return ctx.entity( kv.first );
    const gie::Blackboard* p = ctx.parent();
    while( p )
    {
        for( const auto& kv : p->entities() )
            if( gie::stringRegister().get( kv.second.nameHash() ) == name )
                return ctx.entity( kv.first );
        p = p->parent();
    }
    return nullptr;
}

// Heuristic helper: estimate remaining effort to open the safe.
// Combines travel distance, entry penalties, and missing prerequisites
// (code pieces, tools) so A* steers exploration toward productive actions.
static float EstimateHeistHeuristic( const gie::Simulation& sim, const gie::Entity* agentEnt )
{
    if( !agentEnt ) return 0.f;
    auto agentLoc = agentEnt->property( "Location" );
    if( !agentLoc ) return 0.f;

    // Find safe and its target room
    const gie::Entity* safe = FindEntityByName( sim.context(), "Safe" );
    if( !safe ) return 0.f;

    // If safe is already open, no remaining work
    if( *const_cast< gie::Entity* >( safe )->property( "Open" )->getBool() ) return 0.f;

    Guid safeRoomGuid = *const_cast< gie::Entity* >( safe )->property( "InRoom" )->getGuid();
    const gie::Entity* safeRoomEnt = sim.context().entity( safeRoomGuid );
    if( !safeRoomEnt ) return 0.f;

    auto roomLocP = safeRoomEnt->property( "Location" );
    if( !roomLocP ) return 0.f;

    // Distance to safe room
    std::vector<Guid> wp; if( auto set = sim.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
    auto path = gie::getPath( const_cast< gie::World& >( *sim.world() ), wp, *agentLoc->getVec3(), *roomLocP->getVec3() );
    float h = g_Toggles.travelCostWeight * path.length;

    // Entry penalties when still outside
    auto curRoomGuid = agentEnt->property( "CurrentRoom" ) ? *agentEnt->property( "CurrentRoom" )->getGuid() : gie::NullGuid;
    if( curRoomGuid == gie::NullGuid )
    {
        auto connectors = getAccessPoints( sim.context() );
        bool alarmArmed = true; if( auto a = FindEntityByName( sim.context(), "AlarmSystem" ) ) { alarmArmed = *a->property( "Armed" )->getBool(); }
        if( !connectors.empty() )
        {
            float extra = 0.f;
            for( auto g : connectors )
            {
                auto c = sim.context().entity( g );
                if( *c->property( "Locked" )->getBool() ) extra += g_Toggles.lockedPenalty;
                if( c->property( "Barred" ) && *c->property( "Barred" )->getBool() ) extra += g_Toggles.barredPenalty;
                if( c->property( "Alarmed" ) && alarmArmed && *c->property( "Alarmed" )->getBool() ) extra += g_Toggles.alarmedPenalty;
            }
            h += extra * 0.25f;
        }
    }

    // Prerequisite penalty: estimate cost of actions still needed to open the safe.
    // This guides A* to prefer collecting items before entering when that's cheaper.
    float lockMode = *const_cast< gie::Entity* >( safe )->property( "LockMode" )->getFloat();
    auto inv = agentEnt->property( "Inventory" );
    if( inv )
    {
        auto invArr = inv->getGuidArray();
        if( lockMode == 1.f ) // Code-lock: penalize for each missing code piece
        {
            int required = static_cast< int >( *const_cast< gie::Entity* >( safe )->property( "RequiredCodePieces" )->getFloat() );
            int have = 0;
            for( Guid it : *invArr )
            {
                auto e = sim.context().entity( it );
                if( !e ) continue;
                auto info = e->property( "Info" ); if( !info ) continue;
                auto infoEnt = sim.context().entity( *info->getGuid() );
                if( infoEnt )
                {
                    auto nm = gie::stringRegister().get( infoEnt->nameHash() );
                    if( nm.find( "Code" ) != std::string::npos ) have++;
                }
            }
            int missing = std::max( 0, required - have );
            h += missing * 15.f; // estimated cost per code piece collection trip
        }
    }

    // Alarm penalty: if the alarm is still armed, the agent will need to disable it
    // (required for code-lock safe). Estimate the extra travel + action cost.
    auto alarm = FindEntityByName( sim.context(), "AlarmSystem" );
    if( alarm && *alarm->property( "Armed" )->getBool() && lockMode == 1.f )
    {
        h += 10.f; // estimated cost to reach and disable the alarm panel
    }

    return h;
}

// Set cost on a simulation node: travel distance weighted + base action cost.
// This is the g-cost component used by A* ordering (f = cost + heuristic).
static void SetSimulationCost( gie::Simulation& sim, float travelLength, float baseActionCost )
{
    sim.cost = travelLength * g_Toggles.travelCostWeight + baseActionCost;
}

// Check if a world position is inside the house boundary (WholeHouse room).
// The house spans x:[-30,30], y:[-20,20].
static bool IsPositionInsideHouse( const glm::vec3& pos )
{
    return pos.x >= -30.f && pos.x <= 30.f && pos.y >= -20.f && pos.y <= 20.f;
}

// Check if the agent is currently inside the house.
static bool IsAgentInside( const gie::Blackboard& ctx, Guid agentGuid )
{
    auto agent = ctx.entity( agentGuid );
    if( !agent ) return false;
    auto roomProp = agent->property( "CurrentRoom" );
    return roomProp && *roomProp->getGuid() != gie::NullGuid;
}

// Check if the agent is in the same room as the safe.
static bool IsAgentInSafeRoom( const gie::Blackboard& ctx, Guid agentGuid )
{
    auto agent = ctx.entity( agentGuid );
    if( !agent ) return false;
    auto curRoom = agent->property( "CurrentRoom" );
    if( !curRoom || *curRoom->getGuid() == gie::NullGuid ) return false;
    Guid agentRoom = *curRoom->getGuid();
    auto safe = FindEntityByName( ctx, "Safe" );
    if( !safe ) return false;
    auto inRoom = safe->property( "InRoom" );
    if( !inRoom ) return false;
    return agentRoom == *inRoom->getGuid();
}

// Simple utility: store a movement path to target entity and move agent location.
static float MoveAgentAlongPath( gie::SimulateSimulationParams& params, const glm::vec3& from, gie::Entity* toEntity, const std::vector< Guid >& waypointGuids )
{
    if( !toEntity ) return 0.f;
    auto locPpt = toEntity->property( "Location" );
    if( !locPpt ) return 0.f;
    glm::vec3 to = *locPpt->getVec3();
    auto path = gie::getPath( *params.simulation.world(), waypointGuids, from, to );
    Guid tgt = toEntity->guid();
    auto agentEnt = params.simulation.context().entity( params.agent.guid() );
    gie::storeSimulatedPath( params, path, tgt, agentEnt->property( "Location" )->getVec3() );
    auto agentLoc = agentEnt->property( "Location" )->getVec3();
    if( agentLoc ) *agentLoc = to;
    params.addDebugMessage( "MoveAgentAlongPath -> target=" + std::string( gie::stringRegister().get( toEntity->nameHash() ) ) + ", length=" + std::to_string( path.length ) );
    return path.length;
}

// Build world and return created agent
gie::Agent* heistOpenSafe_world( ExampleParameters& params )
{
    gie::World& world = params.world;
    // specific example visualization
    params.imGuiDrawFunc = &ImGuiFunc6;

    // Agent: the thief
    auto agent = world.createAgent( "Thief" );
    world.context().entityTagRegister().tag( agent, { H( "Agent" ) } );
    agent->createProperty( "Location", glm::vec3{ -30.f, 0.f, 0.f } ); // Outside front vicinity
    agent->createProperty( "CurrentRoom", gie::NullGuid );
    agent->createProperty( "Inventory", gie::Property::GuidVector{} );

    auto selectableTagHash = H( "Selectable" );
	auto drawTagHash = H( "Draw" );

    // Waypoint archetype
    if( auto* a = world.createArchetype( "Waypoint" ) )
    {
		a->addTag( selectableTagHash );
        a->addTag( "Waypoint" );
		a->addTag( drawTagHash );
        a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
        a->addProperty( "Links", gie::Property::GuidVector{} );
    }
    // Room archetype
    if( auto* a = world.createArchetype( "Room" ) )
    {
        a->addTag( "Room" );
        a->addTag( "Draw" );
        a->addProperty( "Vertices", std::vector< glm::vec3 >{} );
        a->addProperty( "Discovered", true );
        a->addProperty( "DisplayName", true );
        a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
    }

    // Item archetype (carryable items)
    if( auto* a = world.createArchetype( "Item" ) )
    {
		a->addTag( selectableTagHash );
        a->addTag( "Item" );
        a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
        a->addProperty( "Info", gie::NullGuid );
    }
    // Info archetype (descriptor entities)
    if( auto* a = world.createArchetype( "Info" ) )
    {
        a->addTag( "Info" );
        a->addProperty( "Name", gie::NullGuid );
    }
    // Safe archetype
    if( auto* a = world.createArchetype( "Safe" ) )
    {
		a->addTag( selectableTagHash );
		a->addTag( drawTagHash );
        a->addProperty( "Open", false );
        a->addProperty( "InRoom", gie::NullGuid );
        a->addProperty( "LockMode", 0.f );
        a->addProperty( "RequiredCodePieces", 0.f );
    }
    // Alarm system and panels
    if( auto* a = world.createArchetype( "AlarmSystem" ) )
    {
        a->addProperty( "Armed", false );
    }
    if( auto* a = world.createArchetype( "AlarmPanel" ) )
    {
		a->addTag( selectableTagHash );
		a->addTag( drawTagHash );
        a->addTag( "AlarmPanel" );
        a->addTag( "Item" );
        a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
    }
    if( auto* a = world.createArchetype( "FuseBoxEntity" ) )
    {
		a->addTag( selectableTagHash );
		a->addTag( drawTagHash );
        a->addTag( "FuseBox" );
        a->addTag( "Item" );
        a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
    }
    // RoomAccess archetype (doors/windows)
    if( auto* a = world.createArchetype( "RoomAccess" ) )
    {
        a->addTag( "Waypoint" );
		a->addTag( selectableTagHash );
		a->addTag( drawTagHash );
        a->addTag( "Access" );
        a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
        a->addProperty( "Links", gie::Property::GuidVector{} );
        a->addProperty( "Locked", false );
        a->addProperty( "Blocked", false );
        a->addProperty( "Barred", false );
        a->addProperty( "Alarmed", false );
        a->addProperty( "RequiredKey", gie::NullGuid );
    }
    // Agent archetype (thief)
    if( auto* a = world.createArchetype( "Agent" ) )
    {
        a->addTag( "Agent" );
        a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
        a->addProperty( "CurrentRoom", gie::NullGuid );
        a->addProperty( "Inventory", gie::Property::GuidVector{} );
    }

    // Room and POI names
    const gie::StringHash AlarmPanelHash    = H( "AlarmPanel" );
    const gie::StringHash BackDoorHash      = H( "BackDoor" );
    const gie::StringHash BathroomHash      = H( "Bathroom" );
    const gie::StringHash BedroomAHash      = H( "BedroomA" );
    const gie::StringHash BedroomBHash      = H( "BedroomB" );
    const gie::StringHash CorridorHash      = H( "Corridor" );
    const gie::StringHash EntranceHash      = H( "Entrance" );
    const gie::StringHash FrontDoorHash     = H( "FrontDoor" );
    const gie::StringHash FuseBoxHash       = H( "FuseBox" );
    const gie::StringHash GarageHash        = H( "Garage" );
    const gie::StringHash KitchenHash       = H( "Kitchen" );
    const gie::StringHash KitchenWindowHash = H( "KitchenWindow" );
    const gie::StringHash LaundryRoomHash   = H( "LaundryRoom" );
    const gie::StringHash LivingRoomHash    = H( "LivingRoom" );
    const gie::StringHash OutsideBackHash   = H( "OutsideBack" );
    const gie::StringHash OutsideFrontHash  = H( "OutsideFront" );
    const gie::StringHash WholeHouseHash    = H( "WholeHouse" );

    struct RoomInfo
    {
        gie::StringHash name;
        glm::vec3 vertices[ 4 ] = {};
    };

    const RoomInfo rooms[] = {
    { WholeHouseHash,  { {  -30.f,  -20.f,   0.f }, {   30.f,  -20.f,   0.f }, {   30.f,   20.f,   0.f }, {  -30.f,   20.f,   0.f } } },
    { LaundryRoomHash, { {  -30.f,  -20.f,   0.f }, {  -20.f,  -20.f,   0.f }, {  -20.f,  -10.f,   0.f }, {  -30.f,  -10.f,   0.f } } },
    { KitchenHash,     { {  -20.f,  -20.f,   0.f }, {   -5.f,  -20.f,   0.f }, {   -5.f,  -10.f,   0.f }, {  -20.f,  -10.f,   0.f } } },
    { BathroomHash,    { {   -5.f,  -20.f,   0.f }, {    5.f,  -20.f,   0.f }, {    5.f,  -10.f,   0.f }, {   -5.f,  -10.f,   0.f } } },
    { GarageHash,      { {    5.f,  -20.f,   0.f }, {   30.f,  -20.f,   0.f }, {   30.f,  -10.f,   0.f }, {    5.f,  -10.f,   0.f } } },
    { CorridorHash,    { {  -30.f,  -10.f,   0.f }, {   30.f,  -10.f,   0.f }, {   30.f,   -5.f,   0.f }, {  -30.f,   -5.f,   0.f } } },
    { LivingRoomHash,  { {  -30.f,   -5.f,   0.f }, {  -10.f,   -5.f,   0.f }, {  -10.f,   20.f,   0.f }, {  -30.f,   20.f,   0.f } } },
    { EntranceHash,    { {  -10.f,   -5.f,   0.f }, {    0.f,   -5.f,   0.f }, {    0.f,   20.f,   0.f }, {  -10.f,   20.f,   0.f } } },
    { BedroomAHash,    { {    0.f,   -5.f,   0.f }, {   15.f,   -5.f,   0.f }, {   15.f,   20.f,   0.f }, {    0.f,   20.f,   0.f } } },
    { BedroomBHash,    { {   15.f,   -5.f,   0.f }, {   30.f,   -5.f,   0.f }, {   30.f,   20.f,   0.f }, {   15.f,   20.f,   0.f } } },
    };

    std::vector< gie::Entity* > roomEntities;
    roomEntities.reserve( std::size( rooms ) );

    for( const auto& room : rooms )
    {
        auto roomEntity = world.createEntity( gie::stringRegister().get( room.name ) );
        roomEntity->createProperty( "Vertices", std::vector< glm::vec3 >( std::begin( room.vertices ), std::end( room.vertices ) ) );
        roomEntity->createProperty( "Discovered", true );
        roomEntity->createProperty( "DisplayName", true );
        roomEntity->createProperty( "Location", ( room.vertices[ 0 ] + room.vertices[ 1 ] + room.vertices[ 2 ] + room.vertices[ 3 ] ) * 0.25f );
        world.context().entityTagRegister().tag( roomEntity, { H( "Room" ), H( "Draw" ) } );
        roomEntities.push_back( roomEntity );
    }

    *( roomEntities[ 0 ]->property( "Discovered" )->getBool() ) = true; // WholeHouse known from start
    *( roomEntities[ 0 ]->property( "DisplayName" )->getBool() ) = false; // don't display WholeHouse name in visualization

    // House systems
    auto alarmSystem = world.createEntity( "AlarmSystem" );
    alarmSystem->createProperty( "Armed", true );

    // Info entities
    auto makeInfo = [&]( const char* infoName ) -> gie::Entity*
    {
        auto e = world.createEntity( infoName );
		e->createProperty( "Name", infoName );
        world.context().entityTagRegister().tag( e, { H( "Info" ) } );
        return e;
    };

    auto infoCrowbar = makeInfo( "CrowbarInfo" );
    auto infoBolt    = makeInfo( "BoltCutterInfo" );
    auto infoLockset = makeInfo( "LockpickInfo" );
    auto infoStetho  = makeInfo( "StethoscopeInfo" );
    auto infoFrontK  = makeInfo( "FrontKeyInfo" );
    auto infoBackK   = makeInfo( "BackKeyInfo" );
    auto infoSafeK   = makeInfo( "SafeKeyInfo" );

    // Set required keys for specific access entities if they exist
    for( const auto& kv : world.context().entities() )
    {
        auto name = gie::stringRegister().get( kv.second.nameHash() );
        if( name == "FrontDoor" )
        {
            auto ent = world.entity( kv.first );
            if( !ent->property( "RequiredKey" ) ) ent->createProperty( "RequiredKey", gie::NullGuid );
            ent->property( "RequiredKey" )->value = infoFrontK->guid();
        }
        else if( name == "BackDoor" )
        {
            auto ent = world.entity( kv.first );
            if( !ent->property( "RequiredKey" ) ) ent->createProperty( "RequiredKey", gie::NullGuid );
            ent->property( "RequiredKey" )->value = infoBackK->guid();
        }
    }

    // Safe entity
    auto safe = world.createEntity( "Safe" );
    safe->createProperty( "Open", false );
    auto safeRoom = FindRoom( world, "BedroomA" );
    safe->createProperty( "InRoom", safeRoom ? safeRoom->guid() : gie::NullGuid );
    safe->createProperty( "LockMode", 1.f );
    safe->createProperty( "RequiredCodePieces", 2.f );

    // Alarm Panel and FuseBox
    auto alarmPanel = world.createEntity( "AlarmPanel" );
    auto alarmRoom = FindRoom( world, "Corridor" );
    alarmPanel->createProperty( "Location", alarmRoom ? *alarmRoom->property( "Location" )->getVec3() : glm::vec3{ 0.f, 0.f, 0.f } );
    world.context().entityTagRegister().tag( alarmPanel, { H( "AlarmPanel" ), H( "Item" ) } );
    auto fuseBox = world.createEntity( "FuseBoxEntity" );
    auto fuseRoom = FindRoom( world, "Garage" );
    fuseBox->createProperty( "Location", fuseRoom ? *fuseRoom->property( "Location" )->getVec3() : glm::vec3{ 0.f, 0.f, 0.f } );
    world.context().entityTagRegister().tag( fuseBox, { H( "FuseBox" ), H( "Item" ) } );

    // Helper to create items placed in rooms
    auto placeItem = [&]( const char* itemName, gie::Entity* info, const char* roomName ) -> gie::Entity*
    {
        auto room = FindRoom( world, roomName );
        auto e = world.createEntity( itemName );
        world.context().entityTagRegister().tag( e, { H( "Item" ) } );
        e->createProperty( "Location", room ? *room->property( "Location" )->getVec3() : glm::vec3{ 0.f, 0.f, 0.f } );
        e->createProperty( "Info", info ? info->guid() : gie::NullGuid );
        return e;
    };

    placeItem( "Crowbar", infoCrowbar, "Garage" );
    placeItem( "BoltCutter", infoBolt, "Garage" );
    placeItem( "LockpickSet", infoLockset, "Garage" );
    placeItem( "Stethoscope", infoStetho, "BedroomB" );
    placeItem( "FrontDoorKey", infoFrontK, "LivingRoom" );
    placeItem( "BackDoorKey", infoBackK, "Kitchen" );
    placeItem( "SafeKey", infoSafeK, "BedroomA" );

    ResetHeistWorldFromToggles( world );

    // Goal: open the safe
    auto safeOpenPpt = safe->property( "Open" );

    // Note: World setup must expose the Goal targets for planner actions.
    // We don't have direct access to params.goal here (the planner will receive it later),
    // but example6 originally appended a target to params.goal. To keep behavior consistent,
    // set the target here by using params.goal (stored at caller side) if available.
    params.goal.targets.emplace_back( safeOpenPpt->guid(), true );

    // Populate the world with waypoints, access points, and additional puzzle elements
    populateWorld( params, agent );

    return agent;
}

// ---------------------------------------------------------------------------
// populateWorld - Creates the navigation mesh, entry points, and multi-stage
// puzzle objects that make the heist scenario compelling.
//
// The mansion layout (from heistOpenSafe_world):
//
//   +------------+---------------+----------+-------------------------+
//   | LaundryRoom|    Kitchen    | Bathroom |         Garage          |
//   | (-25,-15)  |  (-12.5,-15) | (0,-15)  |       (17.5,-15)       |
//   +------------+---------------+----------+-------------------------+
//   |                        Corridor (0, -7.5)                      |
//   +-----------+-----------+---------------+------------------------+
//   |           |           |               |                        |
//   | LivingRoom| Entrance  |   BedroomA    |       BedroomB         |
//   | (-20,7.5) | (-5,7.5)  |   (7.5,7.5)  |     (22.5,7.5)        |
//   |           |           |               |                        |
//   +-----------+-----------+---------------+------------------------+
//
// Entry points:
//   FrontDoor  = between Outside and Entrance (alarmed by default)
//   BackDoor   = between Outside and Kitchen
//   KitchenWindow = between Outside and Kitchen (can be barred)
//   GarageDoor = between Outside and Garage (locked, needs crowbar)
//
// Puzzle layers:
//   1. Entry: get past doors/windows (alarm, locks, bars)
//   2. Navigation: move through rooms using waypoints
//   3. Tool acquisition: find tools scattered in various rooms
//   4. Security: disable alarm system (panel in Corridor or fuse box in Garage)
//   5. Safe access: the safe room may have a locked interior door
//   6. Safe opening: key / code pieces / stethoscope crack / brute force
// ---------------------------------------------------------------------------
void populateWorld( ExampleParameters& params, gie::Agent* agent )
{
    gie::World& world = params.world;

    auto selectableTag = H( "Selectable" );
    auto drawTag       = H( "Draw" );
    auto waypointTag   = H( "Waypoint" );
    auto accessTag     = H( "Access" );
    auto itemTag       = H( "Item" );

    // -- Waypoints ----------------------------------------------------------
    // Navigation nodes placed at room centers and doorways to enable pathfinding.
    // Each waypoint has a Location and Links (bidirectional connections).
    struct WPDef { const char* name; glm::vec3 pos; };
    const WPDef wpDefs[] = {
        // Outside approach points
        { "WP_OutsideFront",   { -5.f,  25.f, 0.f } },   // 0  - in front of house
        { "WP_OutsideBack",    { -12.5f, -25.f, 0.f } },  // 1  - behind house
        { "WP_OutsideGarage",  { 30.f, -15.f, 0.f } },    // 2  - garage side
        // Room centers (match room layout)
        { "WP_Entrance",       { -5.f,   7.5f, 0.f } },   // 3
        { "WP_LivingRoom",     { -20.f,  7.5f, 0.f } },   // 4
        { "WP_Kitchen",        { -12.5f, -15.f, 0.f } },   // 5
        { "WP_LaundryRoom",    { -25.f, -15.f, 0.f } },    // 6
        { "WP_Bathroom",       { 0.f,  -15.f, 0.f } },     // 7
        { "WP_Garage",         { 17.5f, -15.f, 0.f } },    // 8
        { "WP_Corridor",       { 0.f,   -7.5f, 0.f } },    // 9
        { "WP_BedroomA",       { 7.5f,   7.5f, 0.f } },    // 10
        { "WP_BedroomB",       { 22.5f,  7.5f, 0.f } },    // 11
    };
    constexpr size_t wpCount = std::size( wpDefs );

    std::vector<gie::Entity*> wps;
    std::vector<gie::Property::GuidVector*> wpLinks;
    wps.reserve( wpCount );
    wpLinks.reserve( wpCount );

    for( size_t i = 0; i < wpCount; i++ )
    {
        auto e = world.createEntity( wpDefs[i].name );
        world.context().entityTagRegister().tag( e, { waypointTag, drawTag, selectableTag } );
        e->createProperty( "Location", wpDefs[i].pos );
        auto lp = e->createProperty( "Links", gie::Property::GuidVector{} );
        wps.push_back( e );
        wpLinks.push_back( lp->getGuidArray() );
    }

    // Bidirectional link helper
    auto link = [&]( size_t a, size_t b )
    {
        wpLinks[a]->push_back( wps[b]->guid() );
        wpLinks[b]->push_back( wps[a]->guid() );
    };

    // Outside connections
    link( 0, 3 );   // OutsideFront <-> Entrance (through FrontDoor)
    link( 1, 5 );   // OutsideBack  <-> Kitchen   (through BackDoor / KitchenWindow)
    link( 2, 8 );   // OutsideGarage <-> Garage   (through GarageDoor)

    // Interior room connections (all go through Corridor as hub)
    link( 3, 9 );   // Entrance    <-> Corridor
    link( 4, 9 );   // LivingRoom  <-> Corridor
    link( 5, 9 );   // Kitchen     <-> Corridor
    link( 6, 5 );   // LaundryRoom <-> Kitchen
    link( 7, 9 );   // Bathroom    <-> Corridor
    link( 8, 9 );   // Garage      <-> Corridor
    link( 9, 10 );  // Corridor    <-> BedroomA
    link( 9, 11 );  // Corridor    <-> BedroomB
    link( 10, 11 ); // BedroomA    <-> BedroomB

    // -- Access Points (Doors / Windows) ------------------------------------
    // These are the entry connectors the planner must deal with to get inside.
    // Each has Locked/Blocked/Barred/Alarmed flags that gate entry.
    auto makeAccess = [&]( const char* name, glm::vec3 pos ) -> gie::Entity*
    {
        auto e = world.createEntity( name );
        world.context().entityTagRegister().tag( e, { accessTag, waypointTag, drawTag, selectableTag } );
        e->createProperty( "Location", pos );
        e->createProperty( "Links", gie::Property::GuidVector{} );
        e->createProperty( "Locked", false );
        e->createProperty( "Blocked", false );
        e->createProperty( "Barred", false );
        e->createProperty( "Alarmed", false );
        e->createProperty( "RequiredKey", gie::NullGuid );
        return e;
    };

    // FrontDoor: between OutsideFront and Entrance, alarmed by default
    auto frontDoor = makeAccess( "FrontDoor", { -5.f, 20.f, 0.f } );
    frontDoor->property( "Alarmed" )->value = true;

    // BackDoor: between OutsideBack and Kitchen
    auto backDoor = makeAccess( "BackDoor", { -12.5f, -20.f, 0.f } );

    // KitchenWindow: alternative entry near kitchen
    auto kitchenWindow = makeAccess( "KitchenWindow", { -20.f, -20.f, 0.f } );

    // GarageDoor: side entry into garage, locked and blocked by default
    auto garageDoor = makeAccess( "GarageDoor", { 30.f, -15.f, 0.f } );
    garageDoor->property( "Locked" )->value  = true;
    garageDoor->property( "Blocked" )->value = true;

    // Connect access points into the waypoint graph
    auto linkAccess = [&]( gie::Entity* access, size_t wpA, size_t wpB )
    {
        auto al = access->property( "Links" )->getGuidArray();
        al->push_back( wps[wpA]->guid() );
        al->push_back( wps[wpB]->guid() );
        wpLinks[wpA]->push_back( access->guid() );
        wpLinks[wpB]->push_back( access->guid() );
    };

    linkAccess( frontDoor,     0, 3 );  // OutsideFront <-> Entrance
    linkAccess( backDoor,      1, 5 );  // OutsideBack  <-> Kitchen
    linkAccess( kitchenWindow, 1, 5 );  // OutsideBack  <-> Kitchen (alt)
    linkAccess( garageDoor,    2, 8 );  // OutsideGarage <-> Garage

    // Set required keys on doors (link to Info entities created in heistOpenSafe_world)
    auto findEntity = [&]( const char* n ) -> gie::Entity*
    {
        for( auto& kv : world.context().entities() )
            if( gie::stringRegister().get( kv.second.nameHash() ) == n )
                return world.entity( kv.first );
        return nullptr;
    };
    if( auto fki = findEntity( "FrontKeyInfo" ) )
        frontDoor->property( "RequiredKey" )->value = fki->guid();
    if( auto bki = findEntity( "BackKeyInfo" ) )
        backDoor->property( "RequiredKey" )->value = bki->guid();

    // -- Additional puzzle items --------------------------------------------
    // Code pieces: scattered across rooms, needed for code-lock safe mode.
    // The thief must explore multiple rooms to collect enough pieces.
    auto infoCodeA = world.createEntity( "CodePieceAInfo" );
    infoCodeA->createProperty( "Name", "CodePieceAInfo" );
    world.context().entityTagRegister().tag( infoCodeA, { H( "Info" ) } );

    auto infoCodeB = world.createEntity( "CodePieceBInfo" );
    infoCodeB->createProperty( "Name", "CodePieceBInfo" );
    world.context().entityTagRegister().tag( infoCodeB, { H( "Info" ) } );

    auto infoCodeC = world.createEntity( "CodePieceCInfo" );
    infoCodeC->createProperty( "Name", "CodePieceCInfo" );
    world.context().entityTagRegister().tag( infoCodeC, { H( "Info" ) } );

    // Place code pieces — some outside (reachable before entering), some inside
    auto placeCodePieceAt = [&]( const char* itemName, gie::Entity* info, glm::vec3 pos )
    {
        auto e = world.createEntity( itemName );
        world.context().entityTagRegister().tag( e, { itemTag } );
        e->createProperty( "Location", pos );
        e->createProperty( "Info", info->guid() );
    };

    // Code piece A: taped under the mailbox near front approach
    placeCodePieceAt( "CodePieceA", infoCodeA, { -10.f, 23.f, 0.f } );
    // Code piece B: hidden under a garden gnome near the front path
    placeCodePieceAt( "CodePieceB", infoCodeB, { -20.f, 23.f, 0.f } );
    // Code piece C: inside the house (living room) — requires entry first
    auto livingRoom = FindRoom( world, "LivingRoom" );
    placeCodePieceAt( "CodePieceC", infoCodeC, livingRoom ? *livingRoom->property("Location")->getVec3() : glm::vec3{-20.f, 7.5f, 0.f} );

    // -- Relocate existing items to more strategic positions ----------------
    // Move tools to different rooms so the agent must plan routes carefully:
    //   Crowbar     -> LaundryRoom (far corner, useful for breaking doors)
    //   BoltCutter  -> Bathroom    (needed to cut window bars)
    //   LockpickSet -> LivingRoom  (for picking locks on doors)
    //   Stethoscope -> Garage      (for cracking heavy safe, behind locked garage door)
    auto relocate = [&]( const char* itemName, const char* roomName )
    {
        auto item = findEntity( itemName );
        auto room = FindRoom( world, roomName );
        if( item && room )
            item->property( "Location" )->value = *room->property( "Location" )->getVec3();
    };

    relocate( "Crowbar",     "LaundryRoom" );
    relocate( "BoltCutter",  "Bathroom" );
    relocate( "LockpickSet", "LivingRoom" );
    relocate( "Stethoscope", "Garage" );

    // Move SafeKey to BedroomB (opposite side of house from safe in BedroomA)
    relocate( "SafeKey", "BedroomB" );

    // Door keys stay inside the house (LivingRoom / Kitchen) — the agent must
    // already be inside to use them, making them useful only for alternate routes.
    // Code pieces outside are the primary collectibles before entry.

    // Re-apply toggles to set correct lock/alarm state after we created access points
    ResetHeistWorldFromToggles( world );
}

// Register actions and run planner simulate
static int heistOpenSafe_actions( ExampleParameters& params, gie::Agent* agent )
{
    gie::World& world = params.world;
    gie::Planner& planner = params.planner;
    gie::Goal& goal = params.goal;

    // Action dummies
    DEFINE_DUMMY_ACTION_CLASS( DisableAlarm )
    DEFINE_DUMMY_ACTION_CLASS( DisablePower )
    DEFINE_DUMMY_ACTION_CLASS( UseKey )
    DEFINE_DUMMY_ACTION_CLASS( Lockpick )
    DEFINE_DUMMY_ACTION_CLASS( BreakConnector )
    DEFINE_DUMMY_ACTION_CLASS( CutBars )
    DEFINE_DUMMY_ACTION_CLASS( EnterThrough )
    DEFINE_DUMMY_ACTION_CLASS( SearchForItem )
    DEFINE_DUMMY_ACTION_CLASS( MoveInside )
    DEFINE_DUMMY_ACTION_CLASS( OpenSafeWithKey )
    DEFINE_DUMMY_ACTION_CLASS( OpenSafeWithCode )
    DEFINE_DUMMY_ACTION_CLASS( CrackSafe )
    DEFINE_DUMMY_ACTION_CLASS( BruteForceSafe )

    // Simulators
    // Disable the alarm by visiting the alarm panel
    class DisableAlarmSimulator : public gie::ActionSimulator
    {
    public:
        using gie::ActionSimulator::ActionSimulator;
        gie::StringHash hash() const override { return H( "DisableAlarm" ); }
        bool evaluate( gie::EvaluateSimulationParams params ) const override
        {
            params.addDebugMessage( "DisableAlarm::evaluate" );
            // Must be inside the house to reach the alarm panel
            if( !IsAgentInside( params.simulation.context(), params.agent.guid() ) )
            { params.addDebugMessage( "  Agent not inside -> FALSE" ); return false; }
            auto alarm = FindEntityByName( params.simulation.context(), "AlarmSystem" );
            if( !alarm ) { params.addDebugMessage( "  AlarmSystem not found -> FALSE" ); return false; }
            auto armed = alarm->property( "Armed" );
            params.addDebugMessage( "  Armed=" + std::string( *armed->getBool() ? "true" : "false" ) );
            return armed && *armed->getBool();
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "DisableAlarm::simulate" );
            auto& bb = params.simulation.context();
            gie::Entity* panel = FindEntityByName( bb, "AlarmPanel" );
            gie::Entity* alarmE = FindEntityByName( bb, "AlarmSystem" );
            if( !panel || !alarmE ) { params.addDebugMessage( "  Missing panel or system -> FALSE" ); return false; }

            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            auto agentEnt = bb.entity( params.agent.guid() );
            glm::vec3 from = *agentEnt->property( "Location" )->getVec3();
            float len = MoveAgentAlongPath( params, from, panel, wp );
            params.addDebugMessage( "  Move length=" + std::to_string( len ) );

            alarmE->property( "Armed" )->value = false;
            SetSimulationCost( params.simulation, len, 2.f );
            if( auto a = std::make_shared< DisableAlarmAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
            return false;
        }
        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            auto agentEnt = params.simulation.context().entity( params.agent.guid() );
            params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
        }
    };

    // Cut power at the fuse box to effectively disable the alarm too
    class DisablePowerSimulator : public gie::ActionSimulator
    {
    public:
        using gie::ActionSimulator::ActionSimulator;
        gie::StringHash hash() const override { return H( "DisablePower" ); }
        bool evaluate( gie::EvaluateSimulationParams params ) const override
        {
            params.addDebugMessage( "DisablePower::evaluate" );
            // Must be inside the house to reach the fuse box
            if( !IsAgentInside( params.simulation.context(), params.agent.guid() ) )
            { params.addDebugMessage( "  Agent not inside -> FALSE" ); return false; }
            // if alarm is armed, disabling power is useful too
            auto a = FindEntityByName( params.simulation.context(), "AlarmSystem" );
            if( !a ) { params.addDebugMessage( "  AlarmSystem not found -> FALSE" ); return false; }
            bool armed = *a->property( "Armed" )->getBool();
            params.addDebugMessage( std::string( "  Armed=" ) + ( armed ? "true" : "false" ) );
            return armed;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "DisablePower::simulate" );
            auto& bb = params.simulation.context();
            gie::Entity* fuse = FindEntityByName( bb, "FuseBoxEntity" );
            gie::Entity* alarmE = FindEntityByName( bb, "AlarmSystem" );
            if( !fuse || !alarmE ) { params.addDebugMessage( "  Missing fuse or system -> FALSE" ); return false; }
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            auto agentEnt = bb.entity( params.agent.guid() ); glm::vec3 from = *agentEnt->property( "Location" )->getVec3();
            float len = MoveAgentAlongPath( params, from, fuse, wp );
            params.addDebugMessage( "  Move length=" + std::to_string( len ) );
            alarmE->property( "Armed" )->value = false;
            SetSimulationCost( params.simulation, len, 3.f );
            if( auto a = std::make_shared< DisablePowerAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
            return false;
        }
        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            auto agentEnt = params.simulation.context().entity( params.agent.guid() );
            params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
        }
    };

    // Use a matching key in inventory to unlock a door
    class UseKeySimulator : public gie::ActionSimulator
    {
    public: using gie::ActionSimulator::ActionSimulator; gie::StringHash hash() const override { return H( "UseKey" ); }
        bool evaluate( gie::EvaluateSimulationParams params ) const override
        {
            params.addDebugMessage( "UseKey::evaluate" );
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            auto inv = agent->property( "Inventory" )->getGuidArray();
            auto connectors = getAccessPoints( ctx );
            if( connectors.empty() ) { params.addDebugMessage( "  No connectors -> FALSE" ); return false; }
            for( auto g : connectors )
            {
                auto c = ctx.entity( g );
                if( *c->property( "Locked" )->getBool() )
                {
                    auto reqProp = c->property( "RequiredKey" );
                    Guid req = reqProp ? *reqProp->getGuid() : gie::NullGuid;
                    if( req == gie::NullGuid ) continue;
                    for( Guid it : *inv )
                    {
                        if( auto e = ctx.entity( it ) )
                        {
                            auto info = e->property( "Info" );
                            if( info && *info->getGuid() == req ) { params.addDebugMessage( "  Found matching key -> TRUE" ); return true; }
                        }
                    }
                }
            }
            params.addDebugMessage( "  No matching keys or no locked connectors -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "UseKey::simulate" );
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            auto inv = agent->property( "Inventory" )->getGuidArray();
            auto connectors = getAccessPoints( ctx );
            if( connectors.empty() ) return false;
            gie::Entity* bestC = nullptr; float bestL = std::numeric_limits<float>::max();
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            glm::vec3 from = *agent->property( "Location" )->getVec3();
            for( auto g : connectors )
            {
                auto c = ctx.entity( g );
                if( !*c->property( "Locked" )->getBool() ) continue;
                auto reqProp = c->property( "RequiredKey" );
                Guid req = reqProp ? *reqProp->getGuid() : gie::NullGuid;
                if( req == gie::NullGuid ) continue;
                bool have = false;
                for( Guid it : *inv )
                {
                    if( auto e = ctx.entity( it ) )
                    {
                        auto info = e->property( "Info" );
                        if( info && *info->getGuid() == req ) { have = true; break; }
                    }
                }
                if( !have ) continue;
                if( auto loc = c->property( "Location" ) )
                {
                    auto path = gie::getPath( *params.simulation.world(), wp, from, *loc->getVec3() );
                    if( path.length < bestL ) { bestL = path.length; bestC = c; }
                }
            }
            if( !bestC ) { params.addDebugMessage( "  No applicable connector -> FALSE" ); return false; }
            float len = MoveAgentAlongPath( params, from, bestC, wp );
            bestC->property( "Locked" )->value = false;
            SetSimulationCost( params.simulation, len, 1.f );
            if( auto a = std::make_shared< UseKeyAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
            return false;
        }
        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            auto agentEnt = params.simulation.context().entity( params.agent.guid() );
            params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
        }
    };

    // Search world for any item not yet in inventory (except non-carryable POIs)
    class SearchForItemSimulator : public gie::ActionSimulator
    {
    public: using gie::ActionSimulator::ActionSimulator; gie::StringHash hash() const override { return H( "SearchForItem" ); }
        bool evaluate( gie::EvaluateSimulationParams params ) const override
        {
            params.addDebugMessage( "SearchForItem::evaluate" );
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            auto inv = agent->property( "Inventory" )->getGuidArray();
            bool agentInside = IsAgentInside( ctx, params.agent.guid() );
            auto items = ctx.entityTagRegister().tagSet( "Item" );
            if( !items || items->empty() ) { params.addDebugMessage( "  No items -> FALSE" ); return false; }
            for( auto g : *items )
            {
                auto e = ctx.entity( g );
                auto nm = gie::stringRegister().get( e->nameHash() );
                if( nm == "AlarmPanel" || nm == "FuseBoxEntity" ) continue;
                if( e->hasTag( H( "Disabled" ) ) ) continue;
                bool inInv = std::find( inv->begin(), inv->end(), g ) != inv->end();
                if( inInv ) continue;
                // Only pick reachable items: outside items when outside, any item when inside
                auto loc = e->property( "Location" );
                if( loc && !agentInside && IsPositionInsideHouse( *loc->getVec3() ) ) continue;
                params.addDebugMessage( "  Reachable item found -> TRUE" );
                return true;
            }
            params.addDebugMessage( "  No reachable items -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "SearchForItem::simulate" );
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            auto inv = agent->property( "Inventory" )->getGuidArray();
            bool agentInside = IsAgentInside( ctx, params.agent.guid() );
            auto items = ctx.entityTagRegister().tagSet( "Item" );
            if( !items ) return false;
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            glm::vec3 from = *agent->property( "Location" )->getVec3();
            gie::Entity* best = nullptr; float bestL = std::numeric_limits<float>::max();
            for( auto g : *items )
            {
                auto e = ctx.entity( g ); auto nm = gie::stringRegister().get( e->nameHash() );
                if( nm == "AlarmPanel" || nm == "FuseBoxEntity" ) continue;
                if( e->hasTag( H( "Disabled" ) ) ) continue;
                if( std::find( inv->begin(), inv->end(), g ) != inv->end() ) continue;
                // Only pick reachable items based on inside/outside boundary
                auto loc = e->property( "Location" );
                if( loc && !agentInside && IsPositionInsideHouse( *loc->getVec3() ) ) continue;
                if( loc )
                {
                    auto path = gie::getPath( *params.simulation.world(), wp, from, *loc->getVec3() );
                    if( path.length < bestL ) { bestL = path.length; best = e; }
                }
            }
            if( !best ) { params.addDebugMessage( "  No candidate item -> FALSE" ); return false; }
            float len = MoveAgentAlongPath( params, from, best, wp );
            inv->push_back( best->guid() );
            params.addDebugMessage( "  Picked=" + std::string( gie::stringRegister().get( best->nameHash() ) ) + ", len=" + std::to_string( len ) );
            SetSimulationCost( params.simulation, len, 1.f );
            if( auto a = std::make_shared< SearchForItemAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
            return false;
        }
        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            auto agentEnt = params.simulation.context().entity( params.agent.guid() );
            params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
        }
    };

    // Pick a locked connector and open it using lockpicks
    class LockpickSimulator : public gie::ActionSimulator
    {
    public: using gie::ActionSimulator::ActionSimulator; gie::StringHash hash() const override { return H( "Lockpick" ); }
        bool evaluate( gie::EvaluateSimulationParams params ) const override
        {
            params.addDebugMessage( "Lockpick::evaluate" );
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            auto inv = agent->property( "Inventory" )->getGuidArray();
            bool hasLP = false;
            for( auto g : *inv )
            {
                auto e = ctx.entity( g ); auto info = e->property( "Info" ); if( !info ) continue;
                auto ent = ctx.entity( *info->getGuid() ); if( ent && gie::stringRegister().get( ent->nameHash() ) == std::string_view( "LockpickInfo" ) ) { hasLP = true; break; }
            }
            if( !hasLP ) { params.addDebugMessage( "  No lockpick -> FALSE" ); return false; }
            auto connectors = getAccessPoints( ctx ); if( connectors.empty() ) { params.addDebugMessage( "  No connectors -> FALSE" ); return false; }
            for( auto g : connectors ) if( *ctx.entity( g )->property( "Locked" )->getBool() ) return true;
            params.addDebugMessage( "  No locked connector -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "Lockpick::simulate" );
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            auto connectors = getAccessPoints( ctx ); if( connectors.empty() ) return false;
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            glm::vec3 from = *agent->property( "Location" )->getVec3();
            gie::Entity* best = nullptr; float bestL = std::numeric_limits<float>::max();
            for( auto g : connectors )
            {
                auto c = ctx.entity( g ); if( !*c->property( "Locked" )->getBool() ) continue;
                if( auto loc = c->property( "Location" ) )
                {
                    auto path = gie::getPath( *params.simulation.world(), wp, from, *loc->getVec3() );
                    if( path.length < bestL ) { bestL = path.length; best = c; }
                }
            }
            if( !best ) { params.addDebugMessage( "  No target -> FALSE" ); return false; }
            float len = MoveAgentAlongPath( params, from, best, wp );
            best->property( "Locked" )->value = false;
            SetSimulationCost( params.simulation, len, 4.f );
            if( auto a = std::make_shared< LockpickAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
            return false;
        }
        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            auto agentEnt = params.simulation.context().entity( params.agent.guid() );
            params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
        }
    };

    // Cut bars from a barred connector using a bolt cutter
    class CutBarsSimulator : public gie::ActionSimulator
    {
    public: using gie::ActionSimulator::ActionSimulator; gie::StringHash hash() const override { return H( "CutBars" ); }
        bool evaluate( gie::EvaluateSimulationParams params ) const override
        {
            params.addDebugMessage( "CutBars::evaluate" );
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            auto inv = agent->property( "Inventory" )->getGuidArray();
            bool hasBC = false; for( auto g : *inv ) { auto e = ctx.entity( g ); auto info = e->property( "Info" ); if( info ) { auto ent = ctx.entity( *info->getGuid() ); if( ent && gie::stringRegister().get( ent->nameHash() ) == std::string_view( "BoltCutterInfo" ) ) { hasBC = true; break; } } }
            if( !hasBC ) { params.addDebugMessage( "  No bolt cutter -> FALSE" ); return false; }
            auto connectors = getAccessPoints( ctx ); if( connectors.empty() ) { params.addDebugMessage( "  No connectors -> FALSE" ); return false; }
            for( auto g : connectors ) { auto c = ctx.entity( g ); if( c->property( "Barred" ) && *c->property( "Barred" )->getBool() ) return true; }
            params.addDebugMessage( "  No barred connector -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "CutBars::simulate" );
            auto& ctx = params.simulation.context();
            auto connectors = getAccessPoints( ctx ); if( connectors.empty() ) return false;
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            auto agent = ctx.entity( params.agent.guid() ); glm::vec3 from = *agent->property( "Location" )->getVec3();
            gie::Entity* best = nullptr; float bestL = std::numeric_limits<float>::max();
            for( auto g : connectors )
            {
                auto c = ctx.entity( g ); if( !c->property( "Barred" ) || !*c->property( "Barred" )->getBool() ) continue;
                if( auto loc = c->property( "Location" ) ) { auto path = gie::getPath( *params.simulation.world(), wp, from, *loc->getVec3() ); if( path.length < bestL ) { bestL = path.length; best = c; } }
            }
            if( !best ) { params.addDebugMessage( "  No target -> FALSE" ); return false; }
            float len = MoveAgentAlongPath( params, from, best, wp );
            best->property( "Barred" )->value = false;
            SetSimulationCost( params.simulation, len, 5.f );
            if( auto a = std::make_shared< CutBarsAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
            return false;
        }
        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            auto agentEnt = params.simulation.context().entity( params.agent.guid() );
            params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
        }
    };

    // Break any blocking conditions using the crowbar
    class BreakConnectorSimulator : public gie::ActionSimulator
    {
    public: using gie::ActionSimulator::ActionSimulator; gie::StringHash hash() const override { return H( "BreakConnector" ); }
        bool evaluate( gie::EvaluateSimulationParams params ) const override
        {
            params.addDebugMessage( "BreakConnector::evaluate" );
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            auto inv = agent->property( "Inventory" )->getGuidArray();
            bool hasCrowbar = false;
            for( auto g : *inv )
            {
                auto e = ctx.entity( g );
                auto info = e->property( "Info" );
                if( !info ) continue;
                auto infoEnt = ctx.entity( *info->getGuid() );
                if( infoEnt && gie::stringRegister().get( infoEnt->nameHash() ) == std::string_view( "CrowbarInfo" ) ) { hasCrowbar = true; break; }
            }
            if( !hasCrowbar ) { params.addDebugMessage( "  No crowbar -> FALSE" ); return false; }
            auto connectors = getAccessPoints( ctx ); if( connectors.empty() ) { params.addDebugMessage( "  No connectors -> FALSE" ); return false; }
            for( auto g : connectors )
            {
                auto c = ctx.entity( g );
                if( *c->property( "Locked" )->getBool() || (c->property( "Blocked" ) && *c->property( "Blocked" )->getBool()) || (c->property( "Barred" ) && *c->property( "Barred" )->getBool()) ) { params.addDebugMessage( "  Blocked connector found -> TRUE" ); return true; }
            }
            params.addDebugMessage( "  Nothing to break -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "BreakConnector::simulate" );
            auto& ctx = params.simulation.context();
            auto connectors = getAccessPoints( ctx ); if( connectors.empty() ) return false;
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            auto agent = params.simulation.context().entity( params.agent.guid() ); glm::vec3 from = *agent->property( "Location" )->getVec3();
            gie::Entity* best = nullptr; float bestL = std::numeric_limits<float>::max();
            for( auto g : connectors )
            {
                auto c = ctx.entity( g );
                if( !( *c->property( "Locked" )->getBool() || (c->property( "Blocked" ) && *c->property( "Blocked" )->getBool()) || (c->property( "Barred" ) && *c->property( "Barred" )->getBool()) ) ) continue;
                if( auto loc = c->property( "Location" ) )
                {
                    auto path = gie::getPath( *params.simulation.world(), wp, from, *loc->getVec3() );
                    if( path.length < bestL ) { bestL = path.length; best = c; }
                }
            }
            if( !best ) { params.addDebugMessage( "  No target -> FALSE" ); return false; }
            float len = MoveAgentAlongPath( params, from, best, wp );
            best->property( "Locked" )->value = false;
            if( best->property( "Blocked" ) ) best->property( "Blocked" )->value = false;
            if( best->property( "Barred" ) ) best->property( "Barred" )->value = false;
            SetSimulationCost( params.simulation, len, 6.f );
            if( auto a = std::make_shared< BreakConnectorAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
            return false;
        }
        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            auto agentEnt = params.simulation.context().entity( params.agent.guid() );
            params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
        }
    };

    // Cross an entry connector to get inside
    class EnterThroughSimulator : public gie::ActionSimulator
    {
    public: using gie::ActionSimulator::ActionSimulator; gie::StringHash hash() const override { return H( "EnterThrough" ); }
        bool evaluate( gie::EvaluateSimulationParams params ) const override
        {
            params.addDebugMessage( "EnterThrough::evaluate" );
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            if( *agent->property( "CurrentRoom" )->getGuid() != gie::NullGuid ) { params.addDebugMessage( "  Already inside -> FALSE" ); return false; }
            auto connectors = getAccessPoints( ctx ); if( connectors.empty() ) { params.addDebugMessage( "  No connectors -> FALSE" ); return false; }
            bool alarmArmed = true; if( auto a = FindEntityByName( ctx, "AlarmSystem" ) ) { alarmArmed = *a->property( "Armed" )->getBool(); }
            for( auto g : connectors )
            {
                auto c = ctx.entity( g );
                bool blocked = *c->property( "Locked" )->getBool() || (c->property( "Blocked" ) && *c->property( "Blocked" )->getBool()) || (c->property( "Barred" ) && *c->property( "Barred" )->getBool());
                bool alarmed = c->property( "Alarmed" ) && *c->property( "Alarmed" )->getBool();
                if( !blocked && !( alarmArmed && alarmed ) ) { params.addDebugMessage( "  Free connector -> TRUE" ); return true; }
            }
            params.addDebugMessage( "  All entries blocked/alarmed -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "EnterThrough::simulate" );
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            auto connectors = getAccessPoints( ctx ); if( connectors.empty() ) return false;
            bool alarmArmed = true; if( auto a = FindEntityByName( ctx, "AlarmSystem" ) ) { alarmArmed = *a->property( "Armed" )->getBool(); }
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            glm::vec3 from = *agent->property( "Location" )->getVec3();
            gie::Entity* best = nullptr; float bestL = std::numeric_limits<float>::max();
            for( auto g : connectors )
            {
                auto c = ctx.entity( g );
                bool blocked = *c->property( "Locked" )->getBool() || (c->property( "Blocked" ) && *c->property( "Blocked" )->getBool()) || (c->property( "Barred" ) && *c->property( "Barred" )->getBool());
                bool alarmed = c->property( "Alarmed" ) && *c->property( "Alarmed" )->getBool();
                if( blocked || ( alarmArmed && alarmed ) ) continue;
                if( auto loc = c->property( "Location" ) )
                {
                    auto path = gie::getPath( *params.simulation.world(), wp, from, *loc->getVec3() );
                    if( path.length < bestL ) { bestL = path.length; best = c; }
                }
            }
            if( !best ) { params.addDebugMessage( "  No viable entry -> FALSE" ); return false; }
            float len = MoveAgentAlongPath( params, from, best, wp );
            SetSimulationCost( params.simulation, len, 1.f );
            // Determine which room the access point leads into by checking which room
            // contains the access point's location (using the room Vertices bounding box).
            auto accessLoc = *best->property( "Location" )->getVec3();
            gie::Entity* targetRoom = nullptr;
            float bestRoomDist = std::numeric_limits<float>::max();
            auto roomSet = params.simulation.tagSet( "Room" );
            if( roomSet )
            {
                for( auto rg : *roomSet )
                {
                    auto room = ctx.entity( rg );
                    if( !room ) continue;
                    auto nm = gie::stringRegister().get( room->nameHash() );
                    // Skip the whole-house bounding room
                    if( nm == "WholeHouse" ) continue;
                    auto roomLoc = room->property( "Location" );
                    if( !roomLoc ) continue;
                    float dist = glm::length( accessLoc - *roomLoc->getVec3() );
                    if( dist < bestRoomDist ) { bestRoomDist = dist; targetRoom = room; }
                }
            }
            if( targetRoom )
            {
                agent->property( "CurrentRoom" )->value = targetRoom->guid();
                if( auto loc = targetRoom->property( "Location" ) ) { *agent->property( "Location" )->getVec3() = *loc->getVec3(); }
            }
            if( auto a = std::make_shared< EnterThroughAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
            return false;
        }
        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            auto agentEnt = params.simulation.context().entity( params.agent.guid() );
            params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
        }
    };

    // Navigate inside from current room to the safe room
    class MoveInsideSimulator : public gie::ActionSimulator
    {
    public: using gie::ActionSimulator::ActionSimulator; gie::StringHash hash() const override { return H( "MoveInside" ); }
        bool evaluate( gie::EvaluateSimulationParams params ) const override
        {
            params.addDebugMessage( "MoveInside::evaluate" );
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            bool inside = *agent->property( "CurrentRoom" )->getGuid() != gie::NullGuid;
            params.addDebugMessage( std::string( "  Inside=" ) + ( inside ? "true" : "false" ) );
            return inside;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "MoveInside::simulate" );
            // move to safe room if not already there
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            auto cur = *agent->property( "CurrentRoom" )->getGuid();
            gie::Entity* dest = nullptr; const gie::Entity* safe = FindEntityByName( ctx, "Safe" );
            if( safe )
            {
                auto inRoom = *const_cast< gie::Entity* >( safe )->property( "InRoom" )->getGuid();
                if( inRoom != cur ) dest = ctx.entity( inRoom );
            }
            if( !dest ) { params.addDebugMessage( "  Already at safe room or missing -> FALSE" ); return false; }
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            glm::vec3 from = *agent->property( "Location" )->getVec3();
            float len = MoveAgentAlongPath( params, from, dest, wp );
            agent->property( "CurrentRoom" )->value = dest->guid();
            if( auto loc = dest->property( "Location" ) ) *agent->property( "Location" )->getVec3() = *loc->getVec3();
            SetSimulationCost( params.simulation, len, 1.f );
            if( auto a = std::make_shared< MoveInsideAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
            return false;
        }
        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            auto agentEnt = params.simulation.context().entity( params.agent.guid() );
            params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
        }
    };

    // Open safe using a physical key
    class OpenSafeWithKeySimulator : public gie::ActionSimulator
    {
    public: using gie::ActionSimulator::ActionSimulator; gie::StringHash hash() const override { return H( "OpenSafeWithKey" ); }
        bool evaluate( gie::EvaluateSimulationParams params ) const override
        {
            params.addDebugMessage( "OpenSafeWithKey::evaluate" );
            auto& ctx = params.simulation.context();
            // Must be in the safe room
            if( !IsAgentInSafeRoom( ctx, params.agent.guid() ) ) { params.addDebugMessage( "  Not in safe room -> FALSE" ); return false; }
            const gie::Entity* safe = FindEntityByName( ctx, "Safe" );
            if( !safe ) { params.addDebugMessage( "  Safe not found -> FALSE" ); return false; }
            if( *const_cast< gie::Entity* >( safe )->property( "Open" )->getBool() ) { params.addDebugMessage( "  Already open -> FALSE" ); return false; }
            if( *const_cast< gie::Entity* >( safe )->property( "LockMode" )->getFloat() != 0.f ) { params.addDebugMessage( "  Not key-lock -> FALSE" ); return false; }
            auto agent = ctx.entity( params.agent.guid() );
            auto inv = agent->property( "Inventory" )->getGuidArray();
            for( Guid it : *inv )
            {
                auto e = ctx.entity( it );
                auto info = e->property( "Info" ); if( !info ) continue;
                auto infoEnt = ctx.entity( *info->getGuid() );
                if( infoEnt && gie::stringRegister().get( infoEnt->nameHash() ) == std::string_view( "SafeKeyInfo" ) ) return true;
            }
            params.addDebugMessage( "  Missing SafeKey -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "OpenSafeWithKey::simulate" );
            auto& ctx = params.simulation.context();
            gie::Entity* safe = FindEntityByName( ctx, "Safe" );
            if( !safe ) return false;
            safe->property( "Open" )->value = true;
            SetSimulationCost( params.simulation, 0.f, 2.f );
            if( auto a = std::make_shared< OpenSafeWithKeyAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
            return false;
        }
        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            auto agentEnt = params.simulation.context().entity( params.agent.guid() );
            params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
        }
    };

    // Open safe using code pieces — agent must collect enough pieces (RequiredCodePieces)
    // scattered around the property before this action becomes available.
    // The electronic code lock also requires the alarm/security system to be disabled
    // first (it's wired into the same security circuit).
    class OpenSafeWithCodeSimulator : public gie::ActionSimulator
    {
    public: using gie::ActionSimulator::ActionSimulator; gie::StringHash hash() const override { return H( "OpenSafeWithCode" ); }
        bool evaluate( gie::EvaluateSimulationParams params ) const override
        {
            params.addDebugMessage( "OpenSafeWithCode::evaluate" );
            auto& ctx = params.simulation.context();
            // Must be in the safe room
            if( !IsAgentInSafeRoom( ctx, params.agent.guid() ) ) { params.addDebugMessage( "  Not in safe room -> FALSE" ); return false; }
            const gie::Entity* safe = FindEntityByName( ctx, "Safe" );
            if( !safe ) { params.addDebugMessage( "  Safe not found -> FALSE" ); return false; }
            if( *const_cast< gie::Entity* >( safe )->property( "Open" )->getBool() ) { params.addDebugMessage( "  Already open -> FALSE" ); return false; }
            if( *const_cast< gie::Entity* >( safe )->property( "LockMode" )->getFloat() != 1.f ) { params.addDebugMessage( "  Not code-lock -> FALSE" ); return false; }
            // Code lock is wired into the security system — alarm must be disabled first
            auto alarm = FindEntityByName( ctx, "AlarmSystem" );
            if( alarm && *alarm->property( "Armed" )->getBool() ) { params.addDebugMessage( "  Alarm still armed -> FALSE" ); return false; }
            int required = static_cast< int >( *const_cast< gie::Entity* >( safe )->property( "RequiredCodePieces" )->getFloat() );
            auto agent = ctx.entity( params.agent.guid() );
            auto inv = agent->property( "Inventory" )->getGuidArray();
            int codePieces = 0;
            for( Guid it : *inv )
            {
                auto e = ctx.entity( it );
                auto info = e->property( "Info" ); if( !info ) continue;
                auto infoEnt = ctx.entity( *info->getGuid() );
                if( infoEnt )
                {
                    auto nm = gie::stringRegister().get( infoEnt->nameHash() );
                    if( nm.find( "Code" ) != std::string::npos ) codePieces++;
                }
            }
            params.addDebugMessage( "  CodePieces=" + std::to_string( codePieces ) + "/" + std::to_string( required ) );
            if( codePieces < required ) { params.addDebugMessage( "  Not enough code pieces -> FALSE" ); return false; }
            return true;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "OpenSafeWithCode::simulate" );
            auto& ctx = params.simulation.context();
            gie::Entity* safe = FindEntityByName( ctx, "Safe" );
            if( !safe ) return false;
            safe->property( "Open" )->value = true;
            SetSimulationCost( params.simulation, 0.f, 3.f );
            if( auto a = std::make_shared< OpenSafeWithCodeAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
            return false;
        }
        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            auto agentEnt = params.simulation.context().entity( params.agent.guid() );
            params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
        }
    };

    // Crack a heavy-locked safe using a stethoscope (simplified)
    class CrackSafeSimulator : public gie::ActionSimulator
    {
    public: using gie::ActionSimulator::ActionSimulator; gie::StringHash hash() const override { return H( "CrackSafe" ); }
        bool evaluate( gie::EvaluateSimulationParams params ) const override
        {
            params.addDebugMessage( "CrackSafe::evaluate" );
            auto& ctx = params.simulation.context();
            // Must be in the safe room
            if( !IsAgentInSafeRoom( ctx, params.agent.guid() ) ) { params.addDebugMessage( "  Not in safe room -> FALSE" ); return false; }
            const gie::Entity* safe = FindEntityByName( ctx, "Safe" );
            if( !safe ) { params.addDebugMessage( "  Safe not found -> FALSE" ); return false; }
            if( *const_cast< gie::Entity* >( safe )->property( "Open" )->getBool() ) { params.addDebugMessage( "  Already open -> FALSE" ); return false; }
            if( *const_cast< gie::Entity* >( safe )->property( "LockMode" )->getFloat() != 2.f ) { params.addDebugMessage( "  Not heavy-lock -> FALSE" ); return false; }
            auto agent = ctx.entity( params.agent.guid() );
            auto inv = agent->property( "Inventory" )->getGuidArray();
            for( Guid it : *inv )
            {
                auto e = ctx.entity( it );
                auto info = e->property( "Info" ); if( !info ) continue;
                auto infoEnt = ctx.entity( *info->getGuid() );
                if( infoEnt && gie::stringRegister().get( infoEnt->nameHash() ) == std::string_view( "StethoscopeInfo" ) ) return true;
            }
            params.addDebugMessage( "  Missing Stethoscope -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "CrackSafe::simulate" );
            auto& ctx = params.simulation.context();
            gie::Entity* safe = FindEntityByName( ctx, "Safe" );
            if( !safe ) return false;
            safe->property( "Open" )->value = true;
            SetSimulationCost( params.simulation, 0.f, 8.f );
            if( auto a = std::make_shared< CrackSafeAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
            return false;
        }
        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            auto agentEnt = params.simulation.context().entity( params.agent.guid() );
            params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
        }
    };

    // Brute force the safe open — requires a crowbar and being in the safe room.
    // Very high cost: only viable when no better method is available.
    class BruteForceSafeSimulator : public gie::ActionSimulator
    {
    public: using gie::ActionSimulator::ActionSimulator; gie::StringHash hash() const override { return H( "BruteForceSafe" ); }
        bool evaluate( gie::EvaluateSimulationParams params ) const override
        {
            params.addDebugMessage( "BruteForceSafe::evaluate" );
            auto& ctx = params.simulation.context();
            // Must be in the safe room
            if( !IsAgentInSafeRoom( ctx, params.agent.guid() ) ) { params.addDebugMessage( "  Not in safe room -> FALSE" ); return false; }
            const gie::Entity* safe = FindEntityByName( ctx, "Safe" );
            if( !safe ) { params.addDebugMessage( "  Safe not found -> FALSE" ); return false; }
            if( *const_cast< gie::Entity* >( safe )->property( "Open" )->getBool() ) { params.addDebugMessage( "  Already open -> FALSE" ); return false; }
            // Requires crowbar in inventory
            auto agent = ctx.entity( params.agent.guid() );
            auto inv = agent->property( "Inventory" )->getGuidArray();
            for( Guid it : *inv )
            {
                auto e = ctx.entity( it );
                auto info = e->property( "Info" ); if( !info ) continue;
                auto infoEnt = ctx.entity( *info->getGuid() );
                if( infoEnt && gie::stringRegister().get( infoEnt->nameHash() ) == std::string_view( "CrowbarInfo" ) )
                { params.addDebugMessage( "  Have crowbar -> TRUE" ); return true; }
            }
            params.addDebugMessage( "  Missing crowbar -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "BruteForceSafe::simulate" );
            auto& ctx = params.simulation.context();
            gie::Entity* safe = FindEntityByName( ctx, "Safe" );
            if( !safe ) return false;
            // very noisy/slow IRL — extremely high cost, only viable as last resort
            safe->property( "Open" )->value = true;
            SetSimulationCost( params.simulation, 0.f, 200.f );
            if( auto a = std::make_shared< BruteForceSafeAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
            return false;
        }
        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            auto agentEnt = params.simulation.context().entity( params.agent.guid() );
            params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
        }
    };

    // Register action set entries
    DEFINE_ACTION_SET_ENTRY( DisableAlarm )
    DEFINE_ACTION_SET_ENTRY( DisablePower )
    DEFINE_ACTION_SET_ENTRY( UseKey )
    DEFINE_ACTION_SET_ENTRY( Lockpick )
    DEFINE_ACTION_SET_ENTRY( BreakConnector )
    DEFINE_ACTION_SET_ENTRY( CutBars )
    DEFINE_ACTION_SET_ENTRY( EnterThrough )
    DEFINE_ACTION_SET_ENTRY( SearchForItem )
    DEFINE_ACTION_SET_ENTRY( MoveInside )
    DEFINE_ACTION_SET_ENTRY( OpenSafeWithKey )
    DEFINE_ACTION_SET_ENTRY( OpenSafeWithCode )
    DEFINE_ACTION_SET_ENTRY( CrackSafe )
    DEFINE_ACTION_SET_ENTRY( BruteForceSafe )

    planner.addActionSetEntry< DisableAlarmActionSetEntry >( H( "DisableAlarm" ) );
    planner.addActionSetEntry< DisablePowerActionSetEntry >( H( "DisablePower" ) );
    planner.addActionSetEntry< UseKeyActionSetEntry >( H( "UseKey" ) );
    planner.addActionSetEntry< LockpickActionSetEntry >( H( "Lockpick" ) );
    planner.addActionSetEntry< BreakConnectorActionSetEntry >( H( "BreakConnector" ) );
    planner.addActionSetEntry< CutBarsActionSetEntry >( H( "CutBars" ) );
    planner.addActionSetEntry< EnterThroughActionSetEntry >( H( "EnterThrough" ) );
    planner.addActionSetEntry< SearchForItemActionSetEntry >( H( "SearchForItem" ) );
    planner.addActionSetEntry< MoveInsideActionSetEntry >( H( "MoveInside" ) );
    planner.addActionSetEntry< OpenSafeWithKeyActionSetEntry >( H( "OpenSafeWithKey" ) );
    planner.addActionSetEntry< OpenSafeWithCodeActionSetEntry >( H( "OpenSafeWithCode" ) );
    planner.addActionSetEntry< CrackSafeActionSetEntry >( H( "CrackSafe" ) );
    planner.addActionSetEntry< BruteForceSafeActionSetEntry >( H( "BruteForceSafe" ) );

    // Allow deeper search for multi-stage heist puzzle
    planner.depthLimitMutator() = 10;

    // Set initial simulation root
    planner.simulate( goal, *agent );

    return 0;
}

// Thin wrapper preserving original entry point but using split functions
int heistOpenSafe( ExampleParameters& params )
{
    gie::Agent* agent = heistOpenSafe_world( params );
    return heistOpenSafe_actions( params, agent );
}

// Apply UI toggles
static void ResetHeistWorldFromToggles( gie::World& world )
{
    auto get = [&]( const char* n ) -> gie::Entity*
    {
        for( const auto& kv : world.context().entities() )
        {
            if( gie::stringRegister().get( kv.second.nameHash() ) == n ) return world.entity( kv.first );
        }
        return nullptr;
    };

    auto cFront = get( "FrontDoor" );
    auto cBack  = get( "BackDoor" );
    auto cKWin  = get( "KitchenWindow" );

    if( cFront )
    {
        cFront->property( "Locked" )->value  = g_Toggles.frontDoorLocked;
        cFront->property( "Alarmed" )->value = g_Toggles.frontDoorAlarmed;
        cFront->property( "Blocked" )->value = false;
        cFront->property( "Barred" )->value  = false;
    }
    if( cBack )
    {
        cBack->property( "Locked" )->value  = g_Toggles.backDoorLocked;
        cBack->property( "Blocked" )->value = g_Toggles.backDoorBlocked;
        cBack->property( "Alarmed" )->value = false;
        cBack->property( "Barred" )->value  = false;
    }
    if( cKWin )
    {
        cKWin->property( "Locked" )->value  = g_Toggles.kitchenWindowLocked;
        cKWin->property( "Barred" )->value  = g_Toggles.kitchenWindowBarred;
        cKWin->property( "Blocked" )->value = false;
        cKWin->property( "Alarmed" )->value = false;
    }

    // alarm system
    for( const auto& kv : world.context().entities() )
    {
        if( gie::stringRegister().get( kv.second.nameHash() ) == std::string_view( "AlarmSystem" ) )
        {
            world.entity( kv.first )->property( "Armed" )->value = g_Toggles.alarmArmed;
        }
    }

    // Safe placement and mode
    for( const auto& kv : world.context().entities() )
    {
        if( gie::stringRegister().get( kv.second.nameHash() ) == std::string_view( "Safe" ) )
        {
            auto e = world.entity( kv.first );
            e->property( "Open" )->value = false;
            e->property( "LockMode" )->value = static_cast<float>( g_Toggles.safeLockMode );
            const char* roomName = g_Toggles.safeRoomIndex == 0 ? "BedroomA" : "BedroomB";
            for( const auto& kv2 : world.context().entities() )
            {
                if( gie::stringRegister().get( kv2.second.nameHash() ) == roomName )
                {
                    e->property( "InRoom" )->value = kv2.first;
                    break;
                }
            }
            e->property( "RequiredCodePieces" )->value = static_cast<float>( g_Toggles.requiredCodePieces );
            break;
        }
    }

    // Items placement toggles
    auto place = [&]( const char* infoName, const char* itemName, const char* roomName, bool on )
    {
        gie::Entity* info = nullptr; for( const auto& kv : world.context().entities() ) if( gie::stringRegister().get( kv.second.nameHash() ) == infoName ) { info = world.entity( kv.first ); break; }
        gie::Entity* room = FindRoom( world, roomName ); if( !room ) return;
        glm::vec3 roomPos = *room->property( "Location" )->getVec3();
        bool updatedOne = false;
        for( const auto& kv : world.context().entities() )
        {
            if( gie::stringRegister().get( kv.second.nameHash() ) == itemName )
            {
                auto e = world.entity( kv.first );
                if( on )
                {
                    world.context().entityTagRegister().untag( e, { H( "Disabled" ) } );
                    if( auto loc = e->property( "Location" ) ) loc->value = roomPos;
                }
                else
                {
                    world.context().entityTagRegister().tag( e, { H( "Disabled" ) } );
                }
                updatedOne = true;
            }
        }
        if( on && !updatedOne )
        {
            auto e = world.createEntity( itemName );
            world.context().entityTagRegister().tag( e, { H( "Item" ) } );
            e->createProperty( "Location", roomPos );
            e->createProperty( "Info", info ? info->guid() : gie::NullGuid );
        }
    };

    place( "CrowbarInfo",     "Crowbar",     "Garage",     g_Toggles.placeCrowbar );
    place( "BoltCutterInfo",  "BoltCutter",  "Garage",     g_Toggles.placeBoltCutter );
    place( "LockpickInfo",    "LockpickSet", "Garage",     g_Toggles.placeLockpick );
    place( "StethoscopeInfo", "Stethoscope", "BedroomB",   g_Toggles.placeStethoscope );
    place( "FrontKeyInfo",    "FrontDoorKey","LivingRoom", g_Toggles.placeFrontKey );
    place( "BackKeyInfo",     "BackDoorKey", "Kitchen",    g_Toggles.placeBackKey );
    place( "SafeKeyInfo",     "SafeKey",     "BedroomA",   g_Toggles.placeSafeKey );
}

static gie::Entity* FindRoom( gie::World& world, const char* roomName )
{
    for( const auto& kv : world.context().entities() )
    {
        if( gie::stringRegister().get( kv.second.nameHash() ) == roomName ) return world.entity( kv.first );
    }
    return nullptr;
}

static void ImGuiFunc6( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimulationGuid )
{
    ImGui::TextUnformatted( "Heist – Open the Safe (Example 6)" );
    ImGui::Separator();

    if( ImGui::CollapsingHeader( "Entry Points" ) )
    {
        ImGui::Checkbox( "FrontDoor Locked", &g_Toggles.frontDoorLocked ); ImGui::SameLine(); ImGui::Checkbox( "FrontDoor Alarmed", &g_Toggles.frontDoorAlarmed );
        ImGui::Checkbox( "BackDoor Locked", &g_Toggles.backDoorLocked ); ImGui::SameLine(); ImGui::Checkbox( "BackDoor Blocked", &g_Toggles.backDoorBlocked );
        ImGui::Checkbox( "KitchenWindow Locked", &g_Toggles.kitchenWindowLocked ); ImGui::SameLine(); ImGui::Checkbox( "KitchenWindow Barred", &g_Toggles.kitchenWindowBarred );
    }

    if( ImGui::CollapsingHeader( "Systems" ) )
    {
        ImGui::Checkbox( "Alarm Armed", &g_Toggles.alarmArmed );
    }

    if( ImGui::CollapsingHeader( "Safe" ) )
    {
        const char* rooms[] = { "BedroomA", "BedroomB" };
        ImGui::TextUnformatted( "Safe Room" ); ImGui::SameLine(); ImGui::SetNextItemWidth( 120.f ); ImGui::Combo( "##SafeRoom", &g_Toggles.safeRoomIndex, rooms, 2 );
        const char* lockModes[] = { "Key", "Code", "Heavy" };
        ImGui::TextUnformatted( "Lock Type" ); ImGui::SameLine(); ImGui::SetNextItemWidth( 120.f ); ImGui::Combo( "##SafeLock", &g_Toggles.safeLockMode, lockModes, 3 );
        ImGui::SliderInt( "Required Code Pieces", &g_Toggles.requiredCodePieces, 1, 4 );
        ImGui::SliderFloat( "Travel W", &g_Toggles.travelCostWeight, 0.1f, 3.0f );
        ImGui::SliderFloat( "Locked Pen", &g_Toggles.lockedPenalty, 0.f, 30.f );
        ImGui::SliderFloat( "Barred Pen", &g_Toggles.barredPenalty, 0.f, 30.f );
        ImGui::SliderFloat( "Alarmed Pen", &g_Toggles.alarmedPenalty, 0.f, 30.f );
    }

    if( ImGui::CollapsingHeader( "Items" ) )
    {
        ImGui::Checkbox( "Crowbar in Garage", &g_Toggles.placeCrowbar ); ImGui::SameLine(); ImGui::Checkbox( "BoltCutter in Garage", &g_Toggles.placeBoltCutter );
        ImGui::Checkbox( "Lockpick in Garage", &g_Toggles.placeLockpick ); ImGui::SameLine(); ImGui::Checkbox( "Stethoscope in BedroomB", &g_Toggles.placeStethoscope );
        ImGui::Checkbox( "Front Door Key", &g_Toggles.placeFrontKey ); ImGui::SameLine(); ImGui::Checkbox( "Back Door Key", &g_Toggles.placeBackKey ); ImGui::SameLine(); ImGui::Checkbox( "Safe Key", &g_Toggles.placeSafeKey );
    }

    if( ImGui::Button( "Apply Changes" ) )
    {
        ResetHeistWorldFromToggles( world );
    }
    ImGui::SameLine();
    if( ImGui::Button( "Reset Agent" ) )
    {
        auto agent = planner.agent();
        if( agent )
        {
            if( auto p = agent->property( "Location" ) ) *p->getVec3() = glm::vec3{ -30.f, 0.f, 0.f };
            if( auto p = agent->property( "CurrentRoom" ) ) p->value = gie::NullGuid;
            if( auto p = agent->property( "Inventory" ) ) p->getGuidArray()->clear();
        }
    }

    // Reflect important state
    const auto* sim = planner.simulation( selectedSimulationGuid );
    const gie::Blackboard* ctx = sim ? &sim->context() : &world.context();
    auto agentEnt = ctx->entity( planner.agent()->guid() );
    if( agentEnt )
    {
        auto L = *agentEnt->property( "Location" )->getVec3();
        ImGui::Text( "Agent: (%.1f, %.1f)", L.x, L.y );
        auto inv = agentEnt->property( "Inventory" )->getGuidArray();
        size_t invCount = inv ? inv->size() : 0;
        ImGui::Text( "Inventory items: %zu", invCount );
        if( inv && !inv->empty() )
        {
            ImGui::Indent();
            for( auto g : *inv )
            {
                if( auto e = ctx->entity( g ) )
                {
                    ImGui::Text( "- %s", gie::stringRegister().get( e->nameHash() ) );
                }
            }
            ImGui::Unindent();
        }
    }
}
