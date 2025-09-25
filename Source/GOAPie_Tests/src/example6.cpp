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
    // entry points
    bool frontDoorLocked{ false };
    bool frontDoorAlarmed{ true };
    bool backDoorLocked{ false };
    bool backDoorBlocked{ false };
    bool kitchenWindowLocked{ false };
    bool kitchenWindowBarred{ false };

    // systems
    bool alarmArmed{ true };

    // items availability
    bool placeCrowbar{ true };
    bool placeBoltCutter{ true };
    bool placeLockpick{ true };
    bool placeStethoscope{ true };
    bool placeFrontKey{ false };
    bool placeBackKey{ false };
    bool placeSafeKey{ false };

    // safe configuration
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

// Heuristic helper: estimate remaining effort to reach safe room from agent position
static float EstimateHeistHeuristic( const gie::Simulation& sim, const gie::Entity* agentEnt )
{
    if( !agentEnt ) return 0.f;
    auto agentLoc = agentEnt->property( "Location" );
    if( !agentLoc ) return 0.f;

    // find safe and its target room
    const gie::Entity* safe = nullptr;
    for( const auto& kv : sim.context().entities() )
    {
        if( gie::stringRegister().get( kv.second.nameHash() ) == std::string_view( "Safe" ) )
        {
            safe = sim.context().entity( kv.first );
            break;
        }
    }
    if( !safe ) return 0.f;

    Guid safeRoomGuid = *const_cast< gie::Entity* >( safe )->property( "InRoom" )->getGuid();
    const gie::Entity* safeRoomEnt = sim.context().entity( safeRoomGuid );
    if( !safeRoomEnt ) return 0.f;

    auto roomLocP = safeRoomEnt->property( "Location" );
    if( !roomLocP ) return 0.f;

    // collect waypoints
    std::vector<Guid> wp; if( auto set = sim.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
    // world() is const in this context, getPath takes non-const -> cast is fine (read-only)
    auto path = gie::getPath( const_cast< gie::World& >( *sim.world() ), wp, *agentLoc->getVec3(), *roomLocP->getVec3() );
    float h = g_Toggles.travelCostWeight * path.length;

    // add penalties if still outside and connectors are problematic
    auto curRoomGuid = agentEnt->property( "CurrentRoom" ) ? *agentEnt->property( "CurrentRoom" )->getGuid() : gie::NullGuid;
    if( curRoomGuid == gie::NullGuid )
    {
        auto connectors = getAccessPoints( sim.context() );
        bool alarmArmed = true; for( const auto& kv : sim.context().entities() ) { if( gie::stringRegister().get( kv.second.nameHash() ) == std::string_view( "AlarmSystem" ) ) { auto a = sim.context().entity( kv.first ); alarmArmed = *a->property( "Armed" )->getBool(); break; } }
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
            h += extra * 0.25f; // normalize penalties influence
        }
    }
    return h;
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

    return agent;
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
            const auto& ents = params.simulation.context().entities();
            for( const auto& kv : ents )
            {
                const auto& ent = kv.second;
                if( gie::stringRegister().get( ent.nameHash() ) == std::string_view( "AlarmSystem" ) )
                {
                    auto alarm = params.simulation.context().entity( kv.first );
                    auto armed = alarm->property( "Armed" );
                    params.addDebugMessage( "  Armed=" + std::string( *armed->getBool() ? "true" : "false" ) );
                    return armed && *armed->getBool();
                }
            }
            params.addDebugMessage( "  AlarmSystem not found -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "DisableAlarm::simulate" );
            auto& bb = params.simulation.context();
            gie::Entity* panel = nullptr; gie::Entity* alarmE = nullptr;
            for( const auto& kv : bb.entities() )
            {
                const auto& ent = kv.second;
                auto name = gie::stringRegister().get( ent.nameHash() );
                if( name == "AlarmPanel" ) panel = bb.entity( kv.first );
                if( name == "AlarmSystem" ) alarmE = bb.entity( kv.first );
            }
            if( !panel || !alarmE ) { params.addDebugMessage( "  Missing panel or system -> FALSE" ); return false; }

            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            auto agentEnt = bb.entity( params.agent.guid() );
            glm::vec3 from = *agentEnt->property( "Location" )->getVec3();
            float len = MoveAgentAlongPath( params, from, panel, wp );
            params.addDebugMessage( "  Move length=" + std::to_string( len ) );

            alarmE->property( "Armed" )->value = false;
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
            // if alarm is armed, disabling power is useful too
            for( const auto& kv : params.simulation.context().entities() )
            {
                if( gie::stringRegister().get( kv.second.nameHash() ) == std::string_view( "AlarmSystem" ) )
                {
                    auto a = params.simulation.context().entity( kv.first );
                    bool armed = *a->property( "Armed" )->getBool();
                    params.addDebugMessage( std::string( "  Armed=" ) + ( armed ? "true" : "false" ) );
                    return armed;
                }
            }
            params.addDebugMessage( "  AlarmSystem not found -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "DisablePower::simulate" );
            auto& bb = params.simulation.context();
            gie::Entity* fuse = nullptr; gie::Entity* alarmE = nullptr;
            for( const auto& kv : bb.entities() )
            {
                auto nm = gie::stringRegister().get( kv.second.nameHash() );
                if( nm == "FuseBoxEntity" ) fuse = bb.entity( kv.first );
                if( nm == "AlarmSystem" ) alarmE = bb.entity( kv.first );
            }
            if( !fuse || !alarmE ) { params.addDebugMessage( "  Missing fuse or system -> FALSE" ); return false; }
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            auto agentEnt = bb.entity( params.agent.guid() ); glm::vec3 from = *agentEnt->property( "Location" )->getVec3();
            float len = MoveAgentAlongPath( params, from, fuse, wp );
            params.addDebugMessage( "  Move length=" + std::to_string( len ) );
            alarmE->property( "Armed" )->value = false;
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
            MoveAgentAlongPath( params, from, bestC, wp );
            bestC->property( "Locked" )->value = false;
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
            auto items = ctx.entityTagRegister().tagSet( "Item" );
            if( !items || items->empty() ) { params.addDebugMessage( "  No items -> FALSE" ); return false; }
            for( auto g : *items )
            {
                auto e = ctx.entity( g );
                auto nm = gie::stringRegister().get( e->nameHash() );
                if( nm == "AlarmPanel" || nm == "FuseBoxEntity" ) continue;
                if( e->hasTag( H( "Disabled" ) ) ) continue;
                bool inInv = std::find( inv->begin(), inv->end(), g ) != inv->end();
                if( !inInv ) { params.addDebugMessage( "  Missing item found -> TRUE" ); return true; }
            }
            params.addDebugMessage( "  Inventory contains everything -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "SearchForItem::simulate" );
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            auto inv = agent->property( "Inventory" )->getGuidArray();
            auto items = ctx.entityTagRegister().tagSet( "Item" );
            if( !items ) return false;
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            glm::vec3 from = *agent->property( "Location" )->getVec3();
            gie::Entity* best = nullptr; float bestL = std::numeric_limits<float>::max();
            for( auto g : *items )
            {
                auto e = ctx.entity( g ); auto nm = gie::stringRegister().get( e->nameHash() );
                if( nm == "AlarmPanelEntity" || nm == "FuseBoxEntity" ) continue;
                if( e->hasTag( H( "Disabled" ) ) ) continue;
                if( std::find( inv->begin(), inv->end(), g ) != inv->end() ) continue;
                if( auto loc = e->property( "Location" ) )
                {
                    auto path = gie::getPath( *params.simulation.world(), wp, from, *loc->getVec3() );
                    if( path.length < bestL ) { bestL = path.length; best = e; }
                }
            }
            if( !best ) { params.addDebugMessage( "  No candidate item -> FALSE" ); return false; }
            float len = MoveAgentAlongPath( params, from, best, wp );
            inv->push_back( best->guid() );
            params.addDebugMessage( "  Picked=" + std::string( gie::stringRegister().get( best->nameHash() ) ) + ", len=" + std::to_string( len ) );
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
            MoveAgentAlongPath( params, from, best, wp );
            best->property( "Locked" )->value = false;
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
            MoveAgentAlongPath( params, from, best, wp );
            best->property( "Barred" )->value = false;
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
            MoveAgentAlongPath( params, from, best, wp );
            best->property( "Locked" )->value = false;
            if( best->property( "Blocked" ) ) best->property( "Blocked" )->value = false;
            if( best->property( "Barred" ) ) best->property( "Barred" )->value = false;
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
            bool alarmArmed = true; for( const auto& kv : ctx.entities() ) { const auto& ent = kv.second; if( gie::stringRegister().get( ent.nameHash() ) == "AlarmSystem" ) { auto a = ctx.entity( kv.first ); alarmArmed = *a->property( "Armed" )->getBool(); break; } }
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
            bool alarmArmed = true; for( const auto& kv : ctx.entities() ) { const auto& ent = kv.second; if( gie::stringRegister().get( ent.nameHash() ) == "AlarmSystem" ) { auto a = ctx.entity( kv.first ); alarmArmed = *a->property( "Armed" )->getBool(); break; } }
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
            MoveAgentAlongPath( params, from, best, wp );
            // set current room and move location to room center
            // For waypoint-based approach, we need to determine which room this waypoint leads to
            // This is a simplified approach - in a real implementation, waypoints would have From/To properties
            auto agentLoc = *agent->property( "Location" )->getVec3();
            gie::Entity* targetRoom = nullptr;
            // Find the room that contains this waypoint location
            for( const auto& kv : ctx.entities() )
            {
                const auto& ent = kv.second;
                if( gie::stringRegister().get( ent.nameHash() ) == std::string_view( "Entrance" ) ||
                    gie::stringRegister().get( ent.nameHash() ) == std::string_view( "Garage" ) ||
                    gie::stringRegister().get( ent.nameHash() ) == std::string_view( "Kitchen" ) )
                {
                    targetRoom = ctx.entity( kv.first );
                    break;
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
            gie::Entity* dest = nullptr; const gie::Entity* safe = nullptr;
            for( const auto& kv : ctx.entities() )
            {
                const auto& ent = kv.second;
                if( gie::stringRegister().get( ent.nameHash() ) == std::string_view( "Safe" ) ) { safe = ctx.entity( kv.first ); break; }
            }
            if( safe )
            {
                auto inRoom = *const_cast< gie::Entity* >( safe )->property( "InRoom" )->getGuid();
                if( inRoom != cur ) dest = ctx.entity( inRoom );
            }
            if( !dest ) { params.addDebugMessage( "  Already at safe room or missing -> FALSE" ); return false; }
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            glm::vec3 from = *agent->property( "Location" )->getVec3();
            MoveAgentAlongPath( params, from, dest, wp );
            agent->property( "CurrentRoom" )->value = dest->guid();
            if( auto loc = dest->property( "Location" ) ) *agent->property( "Location" )->getVec3() = *loc->getVec3();
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
            const gie::Entity* safe = nullptr; for( const auto& kv : ctx.entities() ) { const auto& ent = kv.second; if( gie::stringRegister().get( ent.nameHash() ) == "Safe" ) { safe = ctx.entity( kv.first ); break; } }
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
            gie::Entity* safe = nullptr; for( const auto& kv : ctx.entities() ) { const auto& ent = kv.second; if( gie::stringRegister().get( ent.nameHash() ) == "Safe" ) { safe = ctx.entity( kv.first ); break; } }
            if( !safe ) return false;
            safe->property( "Open" )->value = true;
            if( auto a = std::make_shared< OpenSafeWithKeyAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
            return false;
        }
        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            auto agentEnt = params.simulation.context().entity( params.agent.guid() );
            params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
        }
    };

    // Open safe using code pieces (simplified: any inventory item with "Code" in info name)
    class OpenSafeWithCodeSimulator : public gie::ActionSimulator
    {
    public: using gie::ActionSimulator::ActionSimulator; gie::StringHash hash() const override { return H( "OpenSafeWithCode" ); }
        bool evaluate( gie::EvaluateSimulationParams params ) const override
        {
            params.addDebugMessage( "OpenSafeWithCode::evaluate" );
            auto& ctx = params.simulation.context();
            const gie::Entity* safe = nullptr; for( const auto& kv : ctx.entities() ) { const auto& ent = kv.second; if( gie::stringRegister().get( ent.nameHash() ) == "Safe" ) { safe = ctx.entity( kv.first ); break; } }
            if( !safe ) { params.addDebugMessage( "  Safe not found -> FALSE" ); return false; }
            if( *const_cast< gie::Entity* >( safe )->property( "Open" )->getBool() ) { params.addDebugMessage( "  Already open -> FALSE" ); return false; }
            if( *const_cast< gie::Entity* >( safe )->property( "LockMode" )->getFloat() != 1.f ) { params.addDebugMessage( "  Not code-lock -> FALSE" ); return false; }
            auto agent = ctx.entity( params.agent.guid() );
            auto inv = agent->property( "Inventory" )->getGuidArray();
            for( Guid it : *inv )
            {
                auto e = ctx.entity( it );
                auto info = e->property( "Info" ); if( !info ) continue;
                auto infoEnt = ctx.entity( *info->getGuid() );
                if( infoEnt )
                {
                    auto nm = gie::stringRegister().get( infoEnt->nameHash() );
                    if( nm.find( "Code" ) != std::string::npos ) return true;
                }
            }
            params.addDebugMessage( "  Missing Code piece -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "OpenSafeWithCode::simulate" );
            auto& ctx = params.simulation.context();
            gie::Entity* safe = nullptr; for( const auto& kv : ctx.entities() ) { const auto& ent = kv.second; if( gie::stringRegister().get( ent.nameHash() ) == "Safe" ) { safe = ctx.entity( kv.first ); break; } }
            if( !safe ) return false;
            safe->property( "Open" )->value = true;
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
            const gie::Entity* safe = nullptr; for( const auto& kv : ctx.entities() ) { const auto& ent = kv.second; if( gie::stringRegister().get( ent.nameHash() ) == "Safe" ) { safe = ctx.entity( kv.first ); break; } }
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
            gie::Entity* safe = nullptr; for( const auto& kv : ctx.entities() ) { const auto& ent = kv.second; if( gie::stringRegister().get( ent.nameHash() ) == "Safe" ) { safe = ctx.entity( kv.first ); break; } }
            if( !safe ) return false;
            safe->property( "Open" )->value = true;
            if( auto a = std::make_shared< CrackSafeAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
            return false;
        }
        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            auto agentEnt = params.simulation.context().entity( params.agent.guid() );
            params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
        }
    };

    // Final brute force fallback (if in the correct room)
    class BruteForceSafeSimulator : public gie::ActionSimulator
    {
    public: using gie::ActionSimulator::ActionSimulator; gie::StringHash hash() const override { return H( "BruteForceSafe" ); }
        bool evaluate( gie::EvaluateSimulationParams params ) const override
        {
            params.addDebugMessage( "BruteForceSafe::evaluate" );
            auto& ctx = params.simulation.context();
            const gie::Entity* safe = nullptr; for( const auto& kv : ctx.entities() ) { const auto& ent = kv.second; if( gie::stringRegister().get( ent.nameHash() ) == "Safe" ) { safe = ctx.entity( kv.first ); break; } }
            if( !safe ) { params.addDebugMessage( "  Safe not found -> FALSE" ); return false; }
            if( *const_cast< gie::Entity* >( safe )->property( "Open" )->getBool() ) { params.addDebugMessage( "  Already open -> FALSE" ); return false; }
            // require being in safe room
            auto agent = ctx.entity( params.agent.guid() );
            auto cur = *agent->property( "CurrentRoom" )->getGuid();
            auto inRoom = *const_cast< gie::Entity* >( safe )->property( "InRoom" )->getGuid();
            bool ok = cur == inRoom;
            params.addDebugMessage( std::string( "  InRoom=" ) + ( ok ? "true" : "false" ) );
            return ok;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "BruteForceSafe::simulate" );
            auto& ctx = params.simulation.context();
            gie::Entity* safe = nullptr; for( const auto& kv : ctx.entities() ) { const auto& ent = kv.second; if( gie::stringRegister().get( ent.nameHash() ) == "Safe" ) { safe = ctx.entity( kv.first ); break; } }
            if( !safe ) return false;
            // very noisy/slow IRL, but here simply open to demonstrate branching
            safe->property( "Open" )->value = true;
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
