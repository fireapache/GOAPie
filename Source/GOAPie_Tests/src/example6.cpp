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

// Simple helpers
static inline float clampf( float v, float lo, float hi ) { return std::max( lo, std::min( v, hi ) ); }

using gie::Guid;

// Common string hashes
static inline gie::StringHash H( const char* s ) { return gie::stringHasher( s ); }

// UI + world knobs for the heist example
struct HeistToggles
{
    // entry points
    bool frontDoorLocked{ true };
    bool frontDoorAlarmed{ true };
    bool backDoorLocked{ true };
    bool backDoorBlocked{ false };
    bool kitchenWindowLocked{ true };
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
        auto connectors = sim.context().entityTagRegister().tagSet( "Connector" );
        bool alarmArmed = true; for( const auto& kv : sim.context().entities() ) { if( gie::stringRegister().get( kv.second.nameHash() ) == std::string_view( "AlarmSystem" ) ) { auto a = sim.context().entity( kv.first ); alarmArmed = *a->property( "Armed" )->getBool(); break; } }
        if( connectors )
        {
            float extra = 0.f;
            for( auto g : *connectors )
            {
                auto c = sim.context().entity( g );
                if( *c->property( "Locked" )->getBool() ) extra += g_Toggles.lockedPenalty;
                if( *c->property( "Barred" )->getBool() ) extra += g_Toggles.barredPenalty;
                if( alarmArmed && *c->property( "Alarmed" )->getBool() ) extra += g_Toggles.alarmedPenalty;
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

// Example entry point
int heistOpenSafe( ExampleParameters& params )
{
    gie::World& world = params.world;
    gie::Planner& planner = params.planner;
    gie::Goal& goal = params.goal;

    // specific example visualization
    params.imGuiDrawFunc = &ImGuiFunc6;

    // Agent: the thief
    auto agent = world.createAgent( "Thief" );
    world.context().entityTagRegister().tag( agent, { H( "Agent" ) } );
    agent->createProperty( "Location", glm::vec3{ -30.f, 0.f, 0.f } ); // Outside front vicinity
    agent->createProperty( "CurrentRoom", gie::NullGuid );
    agent->createProperty( "Inventory", gie::Property::GuidVector{} );

    // Waypoints (rooms + points of interest) laid roughly as a house layout
    struct WP { const char* name; glm::vec3 p; };
    const WP wps[] = {
        { "OutsideFront",  {-30.f,  0.f, 0.f} },
        { "OutsideBack",   { 30.f,  0.f, 0.f} },
        { "Garage",        { 15.f, -5.f, 0.f} },
        { "Kitchen",       {  5.f,  5.f, 0.f} },
        { "Corridor",      {  0.f,  0.f, 0.f} },
        { "LivingRoom",    { -5.f,  8.f, 0.f} },
        { "Bathroom",      { -5.f, -8.f, 0.f} },
        { "BedroomA",      { -12.f,  3.f, 0.f} },
        { "BedroomB",      { -12.f, -3.f, 0.f} },
        { "AlarmPanel",    {  0.f, -2.f, 0.f} },
        { "FuseBox",       { 15.f, -7.f, 0.f} },
        { "FrontDoor",     { -15.f, 0.f, 0.f} },
        { "BackDoor",      {  15.f, 0.f, 0.f} },
        { "KitchenWindow", {  8.f,  7.f, 0.f} }
    };

    std::vector< gie::Entity* > waypoints;
    waypoints.reserve( std::size( wps ) );
    std::vector< Guid > waypointGuids;
    waypointGuids.reserve( std::size( wps ) );

    for( const auto& wp : wps )
    {
        auto e = world.createEntity( wp.name );
        e->createProperty( "Location", wp.p );
        world.context().entityTagRegister().tag( e, { H( "Waypoint" ) } );
        waypoints.push_back( e );
        waypointGuids.push_back( e->guid() );
        // Links array for interior movement graph
        e->createProperty( "Links", gie::Property::GuidVector{} );
    }

    auto findWp = [&]( const char* name ) -> gie::Entity*
    {
        for( auto* e : waypoints ) if( gie::stringRegister().get( e->nameHash() ) == name ) return e;
        return nullptr;
    };

    // Basic interior connectivity (Rooms only)
    auto link = [&]( const char* a, const char* b )
    {
        auto ea = findWp( a ); auto eb = findWp( b );
        if( !ea || !eb ) return;
        auto la = ea->property( "Links" )->getGuidArray(); la->push_back( eb->guid() );
        auto lb = eb->property( "Links" )->getGuidArray(); lb->push_back( ea->guid() );
    };

    link( "Corridor", "Kitchen" );
    link( "Corridor", "LivingRoom" );
    link( "Corridor", "Bathroom" );
    link( "Corridor", "BedroomA" );
    link( "Corridor", "BedroomB" );
    link( "Garage", "Kitchen" ); // internal door from garage to kitchen

    // House systems
    auto alarmSystem = world.createEntity( "AlarmSystem" );
    alarmSystem->createProperty( "Armed", true );

    // Create connectors: FrontDoor, BackDoor, KitchenWindow (entry points)
    auto makeConnector = [&]( const char* name, const char* fromRoom, const char* toRoom, const glm::vec3& where ) -> gie::Entity*
    {
        auto c = world.createEntity( name );
        c->createProperty( "Location", where );
        c->createProperty( "From", findWp( fromRoom ) ? findWp( fromRoom )->guid() : gie::NullGuid );
        c->createProperty( "To",   findWp( toRoom ) ? findWp( toRoom )->guid()   : gie::NullGuid );
        world.context().entityTagRegister().tag( c, { H( "Connector" ) } );
        // state properties
        c->createProperty( "Locked", false );
        c->createProperty( "Blocked", false );
        c->createProperty( "Barred", false );
        c->createProperty( "Alarmed", false );
        // required key info (optional)
        c->createProperty( "RequiredKey", gie::NullGuid );
        return c;
    };

    auto cFront = makeConnector( "FrontDoorConnector", "OutsideFront", "Corridor", *findWp( "FrontDoor" )->property( "Location" )->getVec3() );
    auto cBack  = makeConnector( "BackDoorConnector",  "OutsideBack",  "Kitchen",  *findWp( "BackDoor" )->property( "Location" )->getVec3() );
    auto cKWin  = makeConnector( "KitchenWindowConnector", "OutsideBack", "Kitchen", *findWp( "KitchenWindow" )->property( "Location" )->getVec3() );

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
    cFront->property( "RequiredKey" )->value = infoFrontK->guid();
    cBack->property( "RequiredKey" )->value = infoBackK->guid();

    // Safe entity
    auto safe = world.createEntity( "Safe" );
    safe->createProperty( "Open", false );
    safe->createProperty( "InRoom", findWp( "BedroomA" )->guid() );
    safe->createProperty( "LockMode", 1.f );
    safe->createProperty( "RequiredCodePieces", 2.f );

    // Alarm Panel and FuseBox
    auto alarmPanel = world.createEntity( "AlarmPanelEntity" );
    alarmPanel->createProperty( "Location", *findWp( "AlarmPanel" )->property( "Location" )->getVec3() );
    world.context().entityTagRegister().tag( alarmPanel, { H( "AlarmPanel" ), H( "Item" ) } );
    auto fuseBox = world.createEntity( "FuseBoxEntity" );
    fuseBox->createProperty( "Location", *findWp( "FuseBox" )->property( "Location" )->getVec3() );
    world.context().entityTagRegister().tag( fuseBox, { H( "FuseBox" ), H( "Item" ) } );

    // Helper to create items placed in rooms
    auto placeItem = [&]( const char* itemName, gie::Entity* info, const char* roomName ) -> gie::Entity*
    {
        auto room = findWp( roomName );
        auto e = world.createEntity( itemName );
        world.context().entityTagRegister().tag( e, { H( "Item" ) } );
        e->createProperty( "Location", *room->property( "Location" )->getVec3() );
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
    goal.targets.emplace_back( safeOpenPpt->guid(), true );

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
                if( name == "AlarmPanelEntity" ) panel = bb.entity( kv.first );
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
            auto connectors = ctx.entityTagRegister().tagSet( "Connector" );
            if( !connectors ) { params.addDebugMessage( "  No connectors -> FALSE" ); return false; }
            for( auto g : *connectors )
            {
                auto c = ctx.entity( g );
                if( *c->property( "Locked" )->getBool() )
                {
                    Guid req = *c->property( "RequiredKey" )->getGuid();
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
            auto connectors = ctx.entityTagRegister().tagSet( "Connector" );
            if( !connectors ) return false;
            gie::Entity* bestC = nullptr; float bestL = std::numeric_limits<float>::max();
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            glm::vec3 from = *agent->property( "Location" )->getVec3();
            for( auto g : *connectors )
            {
                auto c = ctx.entity( g );
                if( !*c->property( "Locked" )->getBool() ) continue;
                Guid req = *c->property( "RequiredKey" )->getGuid();
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
                if( nm == "AlarmPanelEntity" || nm == "FuseBoxEntity" ) continue;
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
            auto connectors = ctx.entityTagRegister().tagSet( "Connector" ); if( !connectors ) { params.addDebugMessage( "  No connectors -> FALSE" ); return false; }
            for( auto g : *connectors ) if( *ctx.entity( g )->property( "Locked" )->getBool() ) return true;
            params.addDebugMessage( "  No locked connector -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "Lockpick::simulate" );
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            auto connectors = ctx.entityTagRegister().tagSet( "Connector" ); if( !connectors ) return false;
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            glm::vec3 from = *agent->property( "Location" )->getVec3();
            gie::Entity* best = nullptr; float bestL = std::numeric_limits<float>::max();
            for( auto g : *connectors )
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
            auto connectors = ctx.entityTagRegister().tagSet( "Connector" ); if( !connectors ) { params.addDebugMessage( "  No connectors -> FALSE" ); return false; }
            for( auto g : *connectors ) if( *ctx.entity( g )->property( "Barred" )->getBool() ) return true;
            params.addDebugMessage( "  No barred connector -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "CutBars::simulate" );
            auto& ctx = params.simulation.context();
            auto connectors = ctx.entityTagRegister().tagSet( "Connector" ); if( !connectors ) return false;
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            auto agent = ctx.entity( params.agent.guid() ); glm::vec3 from = *agent->property( "Location" )->getVec3();
            gie::Entity* best = nullptr; float bestL = std::numeric_limits<float>::max();
            for( auto g : *connectors )
            {
                auto c = ctx.entity( g ); if( !*c->property( "Barred" )->getBool() ) continue;
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
            auto connectors = ctx.entityTagRegister().tagSet( "Connector" ); if( !connectors ) { params.addDebugMessage( "  No connectors -> FALSE" ); return false; }
            for( auto g : *connectors )
            {
                auto c = ctx.entity( g );
                if( *c->property( "Locked" )->getBool() || *c->property( "Blocked" )->getBool() || *c->property( "Barred" )->getBool() ) { params.addDebugMessage( "  Blocked connector found -> TRUE" ); return true; }
            }
            params.addDebugMessage( "  Nothing to break -> FALSE" );
            return false;
        }
        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            params.addDebugMessage( "BreakConnector::simulate" );
            auto& ctx = params.simulation.context();
            auto connectors = ctx.entityTagRegister().tagSet( "Connector" ); if( !connectors ) return false;
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            auto agent = params.simulation.context().entity( params.agent.guid() ); glm::vec3 from = *agent->property( "Location" )->getVec3();
            gie::Entity* best = nullptr; float bestL = std::numeric_limits<float>::max();
            for( auto g : *connectors )
            {
                auto c = ctx.entity( g );
                if( !( *c->property( "Locked" )->getBool() || *c->property( "Blocked" )->getBool() || *c->property( "Barred" )->getBool() ) ) continue;
                if( auto loc = c->property( "Location" ) )
                {
                    auto path = gie::getPath( *params.simulation.world(), wp, from, *loc->getVec3() );
                    if( path.length < bestL ) { bestL = path.length; best = c; }
                }
            }
            if( !best ) { params.addDebugMessage( "  No target -> FALSE" ); return false; }
            MoveAgentAlongPath( params, from, best, wp );
            best->property( "Locked" )->value = false;
            best->property( "Blocked" )->value = false;
            best->property( "Barred" )->value = false;
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
            auto connectors = ctx.entityTagRegister().tagSet( "Connector" ); if( !connectors ) { params.addDebugMessage( "  No connectors -> FALSE" ); return false; }
            bool alarmArmed = true; for( const auto& kv : ctx.entities() ) { const auto& ent = kv.second; if( gie::stringRegister().get( ent.nameHash() ) == "AlarmSystem" ) { auto a = ctx.entity( kv.first ); alarmArmed = *a->property( "Armed" )->getBool(); break; } }
            for( auto g : *connectors )
            {
                auto c = ctx.entity( g );
                bool blocked = *c->property( "Locked" )->getBool() || *c->property( "Blocked" )->getBool() || *c->property( "Barred" )->getBool();
                bool alarmed = *c->property( "Alarmed" )->getBool();
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
            auto connectors = ctx.entityTagRegister().tagSet( "Connector" ); if( !connectors ) return false;
            bool alarmArmed = true; for( const auto& kv : ctx.entities() ) { const auto& ent = kv.second; if( gie::stringRegister().get( ent.nameHash() ) == "AlarmSystem" ) { auto a = ctx.entity( kv.first ); alarmArmed = *a->property( "Armed" )->getBool(); break; } }
            std::vector<Guid> wp; if( auto set = params.simulation.tagSet( "Waypoint" ) ) wp.assign( set->begin(), set->end() );
            glm::vec3 from = *agent->property( "Location" )->getVec3();
            gie::Entity* best = nullptr; float bestL = std::numeric_limits<float>::max();
            for( auto g : *connectors )
            {
                auto c = ctx.entity( g );
                bool blocked = *c->property( "Locked" )->getBool() || *c->property( "Blocked" )->getBool() || *c->property( "Barred" )->getBool();
                bool alarmed = *c->property( "Alarmed" )->getBool();
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
            auto toGuid = *best->property( "To" )->getGuid();
            agent->property( "CurrentRoom" )->value = toGuid;
            if( auto room = ctx.entity( toGuid ) )
            {
                if( auto loc = room->property( "Location" ) ) { *agent->property( "Location" )->getVec3() = *loc->getVec3(); }
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

    auto cFront = get( "FrontDoorConnector" );
    auto cBack  = get( "BackDoorConnector" );
    auto cKWin  = get( "KitchenWindowConnector" );

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

    place( "CrowbarInfo",     "Crowbar",     "Garage",    g_Toggles.placeCrowbar );
    place( "BoltCutterInfo",  "BoltCutter",  "Garage",    g_Toggles.placeBoltCutter );
    place( "LockpickInfo",    "LockpickSet", "Garage",    g_Toggles.placeLockpick );
    place( "StethoscopeInfo", "Stethoscope", "BedroomB",  g_Toggles.placeStethoscope );
    place( "FrontKeyInfo",    "FrontDoorKey","LivingRoom",g_Toggles.placeFrontKey );
    place( "BackKeyInfo",     "BackDoorKey", "Kitchen",   g_Toggles.placeBackKey );
    place( "SafeKeyInfo",     "SafeKey",     "BedroomA",  g_Toggles.placeSafeKey );
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
