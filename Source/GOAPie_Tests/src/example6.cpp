#include <functional>
#include <array>
#include <algorithm>
#include <random>
#include <set>
#include <memory>

#include <goapie.h>

#include "example.h"
#include "waypoint_navigation.h"

#include "visualization.h"
#include "gameplay_common.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

// Heist Example (Example 6)
// Multi-plan discovery scenario: the thief starts outside a mansion knowing nothing.
// The gameplay loop cycles between:
//   1. Primary goal: open the safe
//   2. Explore fallback: discover new areas around the mansion
// The agent circles the mansion, discovers an energy panel, disables the alarm,
// enters, collects code pieces, and opens the safe.
//
// Key concepts:
//   - Partial world knowledge via Known tags and Reveals chains
//   - Primary/secondary goals with automatic fallback
//   - Multiple planning cycles with world mutation between plans
//   - ForceLeaf Observe action (opaque during planning)
//   - Exterior exploration ring + interior room navigation

const char* heistOpenSafeDescription()
{
	return "Heist discovery scenario: explore a mansion's perimeter, disable security, and open the safe.";
}

// Simple helpers
static inline float clampf( float v, float lo, float hi ) { return std::max( lo, std::min( v, hi ) ); }

using gie::Guid;

// Common string hashes
static inline gie::StringHash H( const char* s ) { return gie::stringHasher( s ); }

// Forward declarations
static void ImGuiFunc6( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimulationGuid );
static void GLDrawFunc6( gie::World& world, gie::Planner& planner );
static gie::Entity* FindRoom( gie::World& world, const char* roomName );

// ---------------------------------------------------------------------------
// Gameplay Log — records each planning cycle for visualization
// ---------------------------------------------------------------------------
// GameplayCycleEntry is defined in gameplay_common.h

// GameplayLog is defined in gameplay_common.h

static GameplayLog g_GameplayLog;

// Movement paths per cycle per action — stored separately to avoid struct layout change
// that triggers latent heap corruption in GameplayCycleEntry destruction.
// Indexed parallel to g_GameplayLog.cycles[i].actionNames[j].
static std::vector<std::vector<std::vector<glm::vec3>>> g_CycleActionPaths;

// Temporary storage for the path from the last executed movement action.
// ExecuteMoveTo writes here; RunGameplayCycle reads after each action.
static std::vector<glm::vec3> g_LastActionPath;

// UI toggles
struct HeistToggles
{
	float travelCostWeight{ 1.0f };
	bool showFullMap{ false };
};
static HeistToggles g_Toggles{};

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

// Count Known entities that are also waypoints (for display)
static int CountKnown( const gie::Blackboard& ctx )
{
	auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
	if( !knownSet ) return 0;
	return static_cast<int>( knownSet->size() );
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
static float MoveAgentAlongPath( gie::SimulateParams& params, const glm::vec3& from, gie::Entity* toEntity, const std::vector< Guid >& waypointGuids )
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

// Find the nearest linked waypoint to the target entity that is reachable via the nav graph.
// For portal/waypoint entities: uses their Links property.
// For other entities: finds the nearest waypoint in navGraph by distance.
static Guid FindNearestLinkedWP( gie::Blackboard& ctx, gie::Entity* target, const std::vector<Guid>& navGraph, const glm::vec3& agentLoc )
{
	if( !target ) return gie::NullGuid;
	glm::vec3 targetLoc = *target->property( "Location" )->getVec3();

	// For portal/waypoint entities, prefer their Links
	auto links = target->property( "Links" );
	if( links && links->getGuidArray() && !links->getGuidArray()->empty() )
	{
		Guid bestWP = gie::NullGuid;
		float bestDist = std::numeric_limits<float>::max();
		for( Guid lg : *links->getGuidArray() )
		{
			if( std::find( navGraph.begin(), navGraph.end(), lg ) == navGraph.end() ) continue;
			auto wpEnt = ctx.entity( lg );
			if( !wpEnt ) continue;
			float d = glm::distance( agentLoc, *wpEnt->property( "Location" )->getVec3() );
			if( d < bestDist ) { bestDist = d; bestWP = lg; }
		}
		if( bestWP != gie::NullGuid ) return bestWP;
	}

	// Fallback: find nearest navGraph waypoint to the target's location
	Guid bestWP = gie::NullGuid;
	float bestDist = std::numeric_limits<float>::max();
	for( Guid wg : navGraph )
	{
		auto wpEnt = ctx.entity( wg );
		if( !wpEnt ) continue;
		float d = glm::distance( targetLoc, *wpEnt->property( "Location" )->getVec3() );
		if( d < bestDist ) { bestDist = d; bestWP = wg; }
	}
	return bestWP;
}

// Move agent to the nearest linked waypoint of a target entity (not to the entity itself).
// Returns the path length traveled, or -1.f if unreachable.
static float MoveAgentNearEntity( gie::SimulateParams& params, const glm::vec3& from, gie::Entity* toEntity, const std::vector<Guid>& navGraph )
{
	if( !toEntity ) return -1.f;
	auto& ctx = params.simulation.context();
	Guid wpGuid = FindNearestLinkedWP( ctx, toEntity, navGraph, from );
	if( wpGuid == gie::NullGuid ) return -1.f;
	auto wpEnt = ctx.entity( wpGuid );
	if( !wpEnt ) return -1.f;

	glm::vec3 to = *wpEnt->property( "Location" )->getVec3();
	auto path = gie::getPath( *params.simulation.world(), navGraph, from, to );
	if( path.path.empty() && glm::distance( from, to ) > 1.f ) return -1.f;
	Guid tgt = toEntity->guid();
	auto agentEnt = ctx.entity( params.agent.guid() );
	gie::storeSimulatedPath( params, path, tgt, agentEnt->property( "Location" )->getVec3() );
	auto agentLoc = agentEnt->property( "Location" )->getVec3();
	if( agentLoc ) *agentLoc = to;
	params.addDebugMessage( "MoveAgentNear -> " + std::string( gie::stringRegister().get( toEntity->nameHash() ) )
		+ " via " + std::string( gie::stringRegister().get( wpEnt->nameHash() ) ) );
	return path.length;
}

// Set cost on a simulation node: travel distance weighted + base action cost.
static void SetSimulationCost( gie::Simulation& sim, float travelLength, float baseActionCost )
{
	sim.cost = travelLength * g_Toggles.travelCostWeight + baseActionCost;
}

// Build nav graph of Known waypoints, excluding inaccessible portal waypoints.
// Portal waypoints are excluded if locked or (alarm-gated while alarm is armed).
static std::vector<Guid> BuildNavGraph( gie::Blackboard& ctx,
	const std::set<Guid>* wpSet, const std::set<Guid>* portalSet )
{
	std::vector<Guid> navGraph;
	if( !wpSet ) return navGraph;
	auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
	if( !knownSet ) return navGraph;

	auto alarm = FindEntityByName( ctx, "AlarmSystem" );
	bool alarmArmed = alarm && *alarm->property( "Armed" )->getBool();

	for( auto g : *wpSet )
	{
		if( !knownSet->count( g ) ) continue;
		if( portalSet && portalSet->count( g ) )
		{
			auto wp = ctx.entity( g ); if( !wp ) continue;
			if( *wp->property( "Locked" )->getBool() ) continue;
			if( *wp->property( "AlarmGated" )->getBool() && alarmArmed ) continue;
		}
		navGraph.push_back( g );
	}
	return navGraph;
}

// Update agent's CurrentRoom based on position.
static void UpdateAgentRoom( gie::Blackboard& ctx, gie::Entity* agentEnt,
	const std::set<Guid>* roomSet, const glm::vec3& pos )
{
	if( !IsPositionInsideHouse( pos ) )
	{
		agentEnt->property( "CurrentRoom" )->value = gie::NullGuid;
		return;
	}
	if( !roomSet ) return;
	gie::Entity* bestRoom = nullptr;
	float bestDist = std::numeric_limits<float>::max();
	for( auto rg : *roomSet )
	{
		auto room = ctx.entity( rg ); if( !room ) continue;
		if( gie::stringRegister().get( room->nameHash() ) == "WholeHouse" ) continue;
		auto rL = room->property( "Location" ); if( !rL ) continue;
		float rd = glm::distance( pos, *rL->getVec3() );
		if( rd < bestDist ) { bestDist = rd; bestRoom = room; }
	}
	if( bestRoom )
		agentEnt->property( "CurrentRoom" )->value = bestRoom->guid();
}

// Auto-open any closed unlocked portals along a pathfinding result.
static void AutoOpenPortalsOnPath( gie::Blackboard& ctx, const std::set<Guid>* portalSet,
	const std::vector<Guid>& path )
{
	if( !portalSet ) return;
	for( auto wpGuid : path )
	{
		if( !portalSet->count( wpGuid ) ) continue;
		auto portal = ctx.entity( wpGuid ); if( !portal ) continue;
		auto openProp = portal->property( "Open" );
		if( openProp && !*openProp->getBool() )
			openProp->value = true;
	}
}

// ---------------------------------------------------------------------------
// Heuristic: estimate remaining effort to open the safe.
// Combines distance to safe room, alarm penalty, panel unknown penalty,
// outside penalty, and missing code pieces.
// ---------------------------------------------------------------------------
static float EstimateHeistHeuristic( const gie::Simulation& sim, const gie::Entity* agentEnt )
{
	if( !agentEnt ) return 0.f;

	// Find safe
	const gie::Entity* safe = FindEntityByName( sim.context(), "Safe" );
	if( !safe ) return 0.f;
	if( *const_cast< gie::Entity* >( safe )->property( "Open" )->getBool() ) return 0.f;

	auto agentLoc = agentEnt->property( "Location" );
	if( !agentLoc ) return 0.f;

	// Distance to safe room
	Guid safeRoomGuid = *const_cast< gie::Entity* >( safe )->property( "InRoom" )->getGuid();
	const gie::Entity* safeRoomEnt = sim.context().entity( safeRoomGuid );
	float h = 0.f;
	if( safeRoomEnt )
	{
		auto roomLocP = safeRoomEnt->property( "Location" );
		if( roomLocP )
			h += glm::distance( *agentLoc->getVec3(), *roomLocP->getVec3() ) * 0.1f;
	}

	// Alarm penalty: alarm armed adds significant cost
	auto alarm = FindEntityByName( sim.context(), "AlarmSystem" );
	if( alarm && *alarm->property( "Armed" )->getBool() )
		h += 20.f;

	// Panel unknown penalty: if EnergyPanel not known, agent can't disable alarm
	auto knownSet = sim.context().entityTagRegister().tagSet( "Known" );
	auto panelEnt = FindEntityByName( sim.context(), "EnergyPanel" );
	if( panelEnt && knownSet && !knownSet->count( panelEnt->guid() ) )
		h += 15.f;

	// Outside penalty: agent still needs to enter mansion
	auto curRoomGuid = agentEnt->property( "CurrentRoom" ) ? *agentEnt->property( "CurrentRoom" )->getGuid() : gie::NullGuid;
	if( curRoomGuid == gie::NullGuid )
		h += 10.f;

	// Missing required items penalty
	auto inv = agentEnt->property( "Inventory" );
	if( inv )
	{
		auto invArr = inv->getGuidArray();
		auto reqItems = const_cast< gie::Entity* >( safe )->property( "RequiredItems" );
		if( reqItems )
		{
			auto reqArr = reqItems->getGuidArray();
			if( reqArr )
			{
				int missing = 0;
				for( Guid reqInfo : *reqArr )
				{
					bool found = false;
					for( Guid itemG : *invArr )
					{
						auto item = sim.context().entity( itemG );
						if( !item ) continue;
						auto info = item->property( "Info" );
						if( info && *info->getGuid() == reqInfo ) { found = true; break; }
					}
					if( !found ) missing++;
				}
				h += missing * 15.f;
			}
		}
	}

	// Study door locked penalty
	auto studyDoor = FindEntityByName( sim.context(), "StudyDoor" );
	if( studyDoor && *studyDoor->property( "Locked" )->getBool() )
		h += 10.f;

	// Safe not inspected penalty
	auto safeInspected = const_cast< gie::Entity* >( safe )->property( "Inspected" );
	if( safeInspected && !*safeInspected->getBool() )
		h += 5.f;

	return h;
}

// ---------------------------------------------------------------------------
// World setup
// ---------------------------------------------------------------------------
gie::Agent* heistOpenSafe_world( ExampleParameters& params )
{
	gie::World& world = params.world;
	params.imGuiDrawFunc = &ImGuiFunc6;
	params.glDrawFunc = &GLDrawFunc6;
	params.isGameplayExample = true;

	auto selectableTag = H( "Selectable" );
	auto drawTag = H( "Draw" );

	// Agent: the thief
	auto agent = world.createAgent( "Thief" );
	world.context().entityTagRegister().tag( agent, { H( "Agent" ) } );
	agent->createProperty( "Location", glm::vec3{ -50.f, 0.f, 0.f } );  // WP_Start position
	agent->createProperty( "CurrentRoom", gie::NullGuid );
	agent->createProperty( "Inventory", gie::Property::GuidVector{} );
	agent->createProperty( "ExploredNewArea", false );   // exploration goal target
	agent->createProperty( "DiscoveryCount", 1.f );      // UI feedback
	agent->createProperty( "NeededItems", gie::Property::GuidVector{} );

	// -- Archetypes ----------------------------------------------------------
	// Waypoint archetype (exterior navigation nodes)
	if( auto* a = world.createArchetype( "Waypoint" ) )
	{
		a->addTag( selectableTag );
		a->addTag( "Waypoint" );
		a->addTag( drawTag );
		a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
		a->addProperty( "Links", gie::Property::GuidVector{} );
		a->addProperty( "Reveals", gie::Property::GuidVector{} );
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
		a->addTag( selectableTag );
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
		a->addTag( selectableTag );
		a->addTag( drawTag );
		a->addProperty( "Open", false );
		a->addProperty( "InRoom", gie::NullGuid );
		a->addProperty( "LockMode", 0.f );
	}
	// Alarm system
	if( auto* a = world.createArchetype( "AlarmSystem" ) )
	{
		a->addProperty( "Armed", false );
	}
	// Portal archetype (doors/windows connecting waypoints — portals ARE waypoints)
	if( auto* a = world.createArchetype( "Portal" ) )
	{
		a->addTag( selectableTag );
		a->addTag( drawTag );
		a->addTag( "Portal" );
		a->addTag( "Waypoint" );
		a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
		a->addProperty( "SideA", gie::NullGuid );
		a->addProperty( "SideB", gie::NullGuid );
		a->addProperty( "Locked", false );
		a->addProperty( "AlarmGated", false );
		a->addProperty( "RequiredItems", gie::Property::GuidVector{} );
		a->addProperty( "Inspected", false );
		a->addProperty( "Links", gie::Property::GuidVector{} );
		a->addProperty( "Reveals", gie::Property::GuidVector{} );
		a->addProperty( "Open", false );
	}
	// Agent archetype
	if( auto* a = world.createArchetype( "Agent" ) )
	{
		a->addTag( "Agent" );
		a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
		a->addProperty( "CurrentRoom", gie::NullGuid );
		a->addProperty( "Inventory", gie::Property::GuidVector{} );
		a->addProperty( "ExploredNewArea", false );
		a->addProperty( "DiscoveryCount", 0.f );
		a->addProperty( "NeededItems", gie::Property::GuidVector{} );
	}

	// -- Rooms (mansion interior) --------------------------------------------
	const gie::StringHash WholeHouseHash    = H( "WholeHouse" );
	const gie::StringHash LaundryRoomHash   = H( "LaundryRoom" );
	const gie::StringHash KitchenHash       = H( "Kitchen" );
	const gie::StringHash BathroomHash      = H( "Bathroom" );
	const gie::StringHash GarageHash        = H( "Garage" );
	const gie::StringHash CorridorHash      = H( "Corridor" );
	const gie::StringHash LivingRoomHash    = H( "LivingRoom" );
	const gie::StringHash EntranceHash      = H( "Entrance" );
	const gie::StringHash BedroomAHash      = H( "BedroomA" );
	const gie::StringHash BedroomBHash      = H( "BedroomB" );
	const gie::StringHash StudyHash         = H( "Study" );

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
	{ GarageHash,      { {    5.f,  -20.f,   0.f }, {   20.f,  -20.f,   0.f }, {   20.f,  -10.f,   0.f }, {    5.f,  -10.f,   0.f } } },
	{ StudyHash,       { {   20.f,  -20.f,   0.f }, {   30.f,  -20.f,   0.f }, {   30.f,  -10.f,   0.f }, {   20.f,  -10.f,   0.f } } },
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

	*( roomEntities[ 0 ]->property( "Discovered" )->getBool() ) = true;
	*( roomEntities[ 0 ]->property( "DisplayName" )->getBool() ) = false;

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

	auto infoCrowbar  = makeInfo( "CrowbarInfo" );
	auto infoCodeA    = makeInfo( "CodePieceAInfo" );
	auto infoCodeB    = makeInfo( "CodePieceBInfo" );
	auto infoCodeC    = makeInfo( "CodePieceCInfo" );
	auto infoLockpick = makeInfo( "LockpickToolInfo" );

	// Safe entity
	auto safe = world.createEntity( "Safe" );
	safe->createProperty( "Open", false );
	auto safeRoom = FindRoom( world, "BedroomA" );
	safe->createProperty( "InRoom", safeRoom ? safeRoom->guid() : gie::NullGuid );
	safe->createProperty( "LockMode", 1.f );
	safe->createProperty( "RequiredItems", gie::Property::GuidVector{ infoCodeA->guid(), infoCodeB->guid() } );
	safe->createProperty( "Inspected", false );
	safe->createProperty( "Location", safeRoom ? *safeRoom->property( "Location" )->getVec3() : glm::vec3{ 7.5f, 7.5f, 0.f } );
	world.context().entityTagRegister().tag( safe, { selectableTag, drawTag } );

	// Goal: open the safe
	params.goal.targets.emplace_back( safe->property( "Open" )->guid(), true );

	// -- Exterior Waypoints (ring around mansion) ----------------------------
	//
	//                WP_NorthWest ----------- WP_North --------------- WP_NorthEast
	//                  (-35,20)                 (0,30)                   (35,20)
	//                     |                                                |
	//                     |                                                |
	//    WP_West ---------+                  [MANSION]                     +-------- WP_East
	//    (-40,0)          |                                                |         (40,0)
	//       |             |                                                |       [EnergyPanel]
	//       |          WP_SouthWest ------- WP_South ------- WP_SouthEast-+
	//       |           (-35,-25)            (0,-30)           (35,-25)
	//       |
	//    WP_Start
	//    (-50,0)
	//
	struct WPDef { const char* name; glm::vec3 pos; };
	const WPDef extWpDefs[] = {
		{ "WP_Start",     { -50.f,   0.f, 0.f } },   // 0  - agent starts here
		{ "WP_North",     {   0.f,  30.f, 0.f } },   // 1
		{ "WP_NorthEast", {  35.f,  20.f, 0.f } },   // 2
		{ "WP_East",      {  40.f,   0.f, 0.f } },   // 3
		{ "WP_SouthEast", {  35.f, -25.f, 0.f } },   // 4
		{ "WP_South",     {   0.f, -30.f, 0.f } },   // 5
		{ "WP_SouthWest", { -35.f, -25.f, 0.f } },   // 6
		{ "WP_West",      { -40.f,   0.f, 0.f } },   // 7
		{ "WP_NorthWest", { -35.f,  20.f, 0.f } },   // 8
	};
	constexpr size_t extWpCount = std::size( extWpDefs );

	std::vector<gie::Entity*> extWps;
	std::vector<gie::Property::GuidVector*> extWpLinks;
	std::vector<gie::Property::GuidVector*> extWpReveals;
	extWps.reserve( extWpCount );
	extWpLinks.reserve( extWpCount );
	extWpReveals.reserve( extWpCount );

	for( size_t i = 0; i < extWpCount; i++ )
	{
		auto e = world.createEntity( extWpDefs[i].name );
		world.context().entityTagRegister().tag( e, { H( "Waypoint" ), drawTag, selectableTag } );
		e->createProperty( "Location", extWpDefs[i].pos );
		auto lp = e->createProperty( "Links", gie::Property::GuidVector{} );
		auto rp = e->createProperty( "Reveals", gie::Property::GuidVector{} );
		extWps.push_back( e );
		extWpLinks.push_back( lp->getGuidArray() );
		extWpReveals.push_back( rp->getGuidArray() );
	}

	// Exterior bidirectional links (ring connectivity)
	auto linkExt = [&]( size_t a, size_t b )
	{
		extWpLinks[a]->push_back( extWps[b]->guid() );
		extWpLinks[b]->push_back( extWps[a]->guid() );
	};

	linkExt( 0, 7 );  // Start <-> West
	linkExt( 7, 8 );  // West <-> NorthWest
	linkExt( 8, 1 );  // NorthWest <-> North
	linkExt( 1, 2 );  // North <-> NorthEast
	linkExt( 2, 3 );  // NorthEast <-> East
	linkExt( 3, 4 );  // East <-> SouthEast
	linkExt( 4, 5 );  // SouthEast <-> South (shortcut)
	linkExt( 5, 6 );  // South <-> SouthWest
	linkExt( 6, 7 );  // SouthWest <-> West

	// Mark WP_Start as Known (agent starts here)
	world.context().entityTagRegister().tag( extWps[0], { H( "Known" ) } );

	// -- Interior Waypoints --------------------------------------------------
	const WPDef intWpDefs[] = {
		{ "WP_Entrance",    { -5.f,    7.5f, 0.f } },    // 0
		{ "WP_LivingRoom",  { -20.f,   7.5f, 0.f } },    // 1
		{ "WP_Kitchen",     { -12.5f, -15.f, 0.f } },     // 2
		{ "WP_LaundryRoom", { -25.f,  -15.f, 0.f } },     // 3
		{ "WP_Bathroom",    { 0.f,    -15.f, 0.f } },     // 4
		{ "WP_Garage",      { 12.5f,  -15.f, 0.f } },     // 5
		{ "WP_CorridorW",   { -20.f,   -7.5f, 0.f } },    // 6  (west corridor — LivingRoom, Kitchen)
		{ "WP_BedroomA",    { 7.5f,    7.5f, 0.f } },     // 7
		{ "WP_BedroomB",    { 22.5f,   7.5f, 0.f } },     // 8
		{ "WP_Study",       { 25.f,   -15.f, 0.f } },     // 9
		{ "WP_CorridorCW",  { -5.f,   -7.5f, 0.f } },     // 10 (center-west — Entrance, Kitchen, Bathroom)
		{ "WP_CorridorCE",  { 10.f,   -7.5f, 0.f } },     // 11 (center-east — BedroomA, Garage)
		{ "WP_CorridorE",   { 22.5f,  -7.5f, 0.f } },     // 12 (east corridor — BedroomB, Study)
	};
	constexpr size_t intWpCount = std::size( intWpDefs );

	std::vector<gie::Entity*> intWps;
	std::vector<gie::Property::GuidVector*> intWpLinks;
	std::vector<gie::Property::GuidVector*> intWpReveals;
	intWps.reserve( intWpCount );
	intWpLinks.reserve( intWpCount );
	intWpReveals.reserve( intWpCount );

	for( size_t i = 0; i < intWpCount; i++ )
	{
		auto e = world.createEntity( intWpDefs[i].name );
		world.context().entityTagRegister().tag( e, { H( "Waypoint" ), drawTag, selectableTag } );
		e->createProperty( "Location", intWpDefs[i].pos );
		auto lp = e->createProperty( "Links", gie::Property::GuidVector{} );
		auto rp = e->createProperty( "Reveals", gie::Property::GuidVector{} );
		intWps.push_back( e );
		intWpLinks.push_back( lp->getGuidArray() );
		intWpReveals.push_back( rp->getGuidArray() );
	}

	// NOTE: No direct interior waypoint links — ALL inter-room connectivity
	// goes through portal waypoints (portals ARE waypoints with Links to both sides).

	// -- Portal Entities (Doors/Windows) — connect waypoints -------------------
	auto makePortal = [&]( const char* name, gie::Entity* sideA, gie::Entity* sideB,
		bool locked = false, bool alarmGated = false, bool open = false ) -> gie::Entity*
	{
		auto p = world.createEntity( name );
		world.context().entityTagRegister().tag( p, { H( "Portal" ), H( "Waypoint" ), selectableTag, drawTag } );
		auto locA = *sideA->property( "Location" )->getVec3();
		auto locB = *sideB->property( "Location" )->getVec3();
		p->createProperty( "Location", ( locA + locB ) * 0.5f );
		p->createProperty( "SideA", sideA->guid() );
		p->createProperty( "SideB", sideB->guid() );
		p->createProperty( "Locked", locked );
		p->createProperty( "AlarmGated", alarmGated );
		p->createProperty( "RequiredItems", gie::Property::GuidVector{} );
		p->createProperty( "Inspected", false );
		p->createProperty( "Open", open );
		// Portal is a waypoint — set up bidirectional links to both sides
		auto portalLinks = p->createProperty( "Links", gie::Property::GuidVector{} );
		portalLinks->getGuidArray()->push_back( sideA->guid() );
		portalLinks->getGuidArray()->push_back( sideB->guid() );
		// Also add portal to sideA and sideB's link lists
		sideA->property( "Links" )->getGuidArray()->push_back( p->guid() );
		sideB->property( "Links" )->getGuidArray()->push_back( p->guid() );
		p->createProperty( "Reveals", gie::Property::GuidVector{} );
		return p;
	};

	// Exterior portals (alarm-gated) — positioned on mansion outer walls
	auto frontDoor     = makePortal( "FrontDoor",     extWps[1], intWps[0], false, true );   // WP_North     <-> WP_Entrance
	auto backDoor      = makePortal( "BackDoor",      extWps[5], intWps[2], false, true );   // WP_South     <-> WP_Kitchen
	auto kitchenWindow = makePortal( "KitchenWindow", extWps[5], intWps[2], false, true );   // WP_South     <-> WP_Kitchen
	auto garageDoor    = makePortal( "GarageDoor",    extWps[4], intWps[5], false, true );   // WP_SouthEast <-> WP_Garage

	// Interior portals — connected to nearest corridor segment
	auto doorEntrCorridor   = makePortal( "Door_EntrCorridor",   intWps[0],  intWps[10] );        // Entrance    <-> CorridorCW
	auto doorLivingCorridor = makePortal( "Door_LivingCorridor", intWps[1],  intWps[6] );         // LivingRoom  <-> CorridorW
	auto doorKitchCorridor  = makePortal( "Door_KitchCorridor",  intWps[2],  intWps[10] );        // Kitchen     <-> CorridorCW
	auto doorLaundryKitch   = makePortal( "Door_LaundryKitch",   intWps[3],  intWps[2] );         // LaundryRoom <-> Kitchen
	auto doorBathCorridor   = makePortal( "Door_BathCorridor",   intWps[4],  intWps[10] );        // Bathroom    <-> CorridorCW
	auto doorGarageCorridor = makePortal( "Door_GarageCorridor", intWps[5],  intWps[11] );        // Garage      <-> CorridorCE
	auto doorBedACorridor   = makePortal( "Door_BedACorridor",   intWps[7],  intWps[11] );        // BedroomA    <-> CorridorCE
	auto doorBedBCorridor   = makePortal( "Door_BedBCorridor",   intWps[8],  intWps[12] );        // BedroomB    <-> CorridorE
	auto doorBedABedB       = makePortal( "Door_BedABedB",       intWps[7],  intWps[8] );         // BedroomA    <-> BedroomB
	auto studyDoor          = makePortal( "StudyDoor",           intWps[12], intWps[9], true );   // CorridorE   <-> Study [Locked]

	// StudyDoor requires lockpick tool
	*studyDoor->property( "RequiredItems" )->getGuidArray() = { infoLockpick->guid() };

	// Corridor passage portals (open archways within the corridor room — always open)
	auto passageW_CW  = makePortal( "Passage_W_CW",  intWps[6],  intWps[10], false, false, true );   // CorridorW  <-> CorridorCW
	auto passageCW_CE = makePortal( "Passage_CW_CE", intWps[10], intWps[11], false, false, true );   // CorridorCW <-> CorridorCE
	auto passageCE_E  = makePortal( "Passage_CE_E",  intWps[11], intWps[12], false, false, true );   // CorridorCE <-> CorridorE

	// Override portal positions to sit on actual room walls
	auto setPos = [&]( gie::Entity* e, glm::vec3 p ) { *e->property( "Location" )->getVec3() = p; };
	// Exterior portals — on mansion outer walls
	setPos( frontDoor,     { -5.f,   20.f, 0.f } );   // Entrance north wall
	setPos( backDoor,      { -15.f, -20.f, 0.f } );   // Kitchen south wall (west side)
	setPos( kitchenWindow, { -8.f,  -20.f, 0.f } );   // Kitchen south wall (east side)
	setPos( garageDoor,    { 12.5f, -20.f, 0.f } );   // Garage south wall
	// Interior portals — on shared walls between rooms
	setPos( doorEntrCorridor,   { -5.f,   -5.f, 0.f } );    // Entrance/Corridor wall at y=-5
	setPos( doorLivingCorridor, { -20.f,  -5.f, 0.f } );    // LivingRoom/Corridor wall at y=-5
	setPos( doorKitchCorridor,  { -12.5f, -10.f, 0.f } );   // Kitchen/Corridor wall at y=-10
	setPos( doorLaundryKitch,   { -20.f,  -15.f, 0.f } );   // LaundryRoom/Kitchen wall at x=-20
	setPos( doorBathCorridor,   { 0.f,    -10.f, 0.f } );   // Bathroom/Corridor wall at y=-10
	setPos( doorGarageCorridor, { 12.5f,  -10.f, 0.f } );   // Garage/Corridor wall at y=-10
	setPos( doorBedACorridor,   { 7.5f,   -5.f, 0.f } );    // BedroomA/Corridor wall at y=-5
	setPos( doorBedBCorridor,   { 22.5f,  -5.f, 0.f } );    // BedroomB/Corridor wall at y=-5
	setPos( doorBedABedB,       { 15.f,    7.5f, 0.f } );   // BedroomA/BedroomB wall at x=15
	setPos( studyDoor,          { 25.f,  -10.f, 0.f } );    // Study/Corridor wall at y=-10

	// -- EnergyPanel (near WP_East) ------------------------------------------
	auto energyPanel = world.createEntity( "EnergyPanel" );
	world.context().entityTagRegister().tag( energyPanel, { selectableTag, drawTag } );
	energyPanel->createProperty( "Location", glm::vec3{ 40.f, 5.f, 0.f } );
	energyPanel->createProperty( "Open", false );  // panel door closed initially

	// -- Items ---------------------------------------------------------------
	auto placeItemAt = [&]( const char* itemName, gie::Entity* info, glm::vec3 pos ) -> gie::Entity*
	{
		auto e = world.createEntity( itemName );
		world.context().entityTagRegister().tag( e, { H( "Item" ), selectableTag } );
		e->createProperty( "Location", pos );
		e->createProperty( "Info", info->guid() );
		return e;
	};

	// Code pieces
	auto codePieceA = placeItemAt( "CodePieceA", infoCodeA, { -5.f, 28.f, 0.f } );    // near North/FrontDoor (exterior)
	auto codePieceB = placeItemAt( "CodePieceB", infoCodeB, { 25.f, -15.f, 0.f } );   // in Study room
	// Code piece C inside (in LivingRoom)
	auto livingRoom = FindRoom( world, "LivingRoom" );
	auto codePieceC = placeItemAt( "CodePieceC", infoCodeC, livingRoom ? *livingRoom->property( "Location" )->getVec3() : glm::vec3{ -20.f, 7.5f, 0.f } );
	// Crowbar inside (in LaundryRoom)
	auto laundryRoom = FindRoom( world, "LaundryRoom" );
	auto crowbar = placeItemAt( "Crowbar", infoCrowbar, laundryRoom ? *laundryRoom->property( "Location" )->getVec3() : glm::vec3{ -25.f, -15.f, 0.f } );
	// BobbyPin in Bathroom (for lockpicking StudyDoor)
	auto bobbyPin = placeItemAt( "BobbyPin", infoLockpick, { 0.f, -15.f, 0.f } );  // Bathroom center

	// -- Reveal chain --------------------------------------------------------
	// What each exterior waypoint reveals when Observed:
	// Start(Known) -> West
	extWpReveals[0]->push_back( extWps[7]->guid() );   // Start reveals West

	// West -> NorthWest
	extWpReveals[7]->push_back( extWps[8]->guid() );   // West reveals NorthWest

	// NorthWest -> North, South
	extWpReveals[8]->push_back( extWps[1]->guid() );   // NorthWest reveals North
	extWpReveals[8]->push_back( extWps[5]->guid() );   // NorthWest reveals South

	// North -> NorthEast, FrontDoor, CodePieceA, WP_Entrance (visible through door)
	extWpReveals[1]->push_back( extWps[2]->guid() );   // North reveals NorthEast
	extWpReveals[1]->push_back( frontDoor->guid() );    // North reveals FrontDoor
	extWpReveals[1]->push_back( codePieceA->guid() );   // North reveals CodePieceA
	extWpReveals[1]->push_back( intWps[0]->guid() );    // North reveals WP_Entrance

	// NorthEast -> East
	extWpReveals[2]->push_back( extWps[3]->guid() );   // NorthEast reveals East

	// East -> SouthEast, EnergyPanel
	extWpReveals[3]->push_back( extWps[4]->guid() );    // East reveals SouthEast
	extWpReveals[3]->push_back( energyPanel->guid() );   // East reveals EnergyPanel

	// SouthEast -> GarageDoor, WP_Garage (visible through south-facing garage door)
	extWpReveals[4]->push_back( garageDoor->guid() );    // SouthEast reveals GarageDoor
	extWpReveals[4]->push_back( intWps[5]->guid() );     // SouthEast reveals WP_Garage

	// South -> SouthWest, BackDoor, KitchenWindow, WP_Kitchen (visible through window)
	extWpReveals[5]->push_back( extWps[6]->guid() );   // South reveals SouthWest
	extWpReveals[5]->push_back( backDoor->guid() );     // South reveals BackDoor
	extWpReveals[5]->push_back( kitchenWindow->guid() );// South reveals KitchenWindow
	extWpReveals[5]->push_back( intWps[2]->guid() );    // South reveals WP_Kitchen

	// -- Interior Reveal Chain -----------------------------------------------
	// WP_Kitchen -> Door_KitchCorridor, WP_CorridorCW, Door_LaundryKitch
	intWpReveals[2]->push_back( doorKitchCorridor->guid() );
	intWpReveals[2]->push_back( intWps[10]->guid() );     // WP_CorridorCW
	intWpReveals[2]->push_back( doorLaundryKitch->guid() );

	// Corridor reveal set — the corridor is one open room, so standing anywhere
	// reveals all doors, rooms visible through doors, and corridor passages.
	// Duplicated across all 4 corridor waypoints so any entry point works.
	std::vector<gie::Guid> corridorRevealSet;
	// Doors on corridor walls
	corridorRevealSet.push_back( doorEntrCorridor->guid() );
	corridorRevealSet.push_back( doorLivingCorridor->guid() );
	corridorRevealSet.push_back( doorKitchCorridor->guid() );
	corridorRevealSet.push_back( doorBathCorridor->guid() );
	corridorRevealSet.push_back( doorGarageCorridor->guid() );
	corridorRevealSet.push_back( doorBedACorridor->guid() );
	corridorRevealSet.push_back( doorBedBCorridor->guid() );
	corridorRevealSet.push_back( studyDoor->guid() );
	corridorRevealSet.push_back( doorBedABedB->guid() );
	// Room waypoints visible through open doors
	corridorRevealSet.push_back( intWps[0]->guid() );      // WP_Entrance
	corridorRevealSet.push_back( intWps[1]->guid() );      // WP_LivingRoom
	corridorRevealSet.push_back( intWps[4]->guid() );      // WP_Bathroom
	corridorRevealSet.push_back( intWps[7]->guid() );      // WP_BedroomA
	corridorRevealSet.push_back( intWps[8]->guid() );      // WP_BedroomB
	// Corridor passages
	corridorRevealSet.push_back( passageW_CW->guid() );
	corridorRevealSet.push_back( passageCW_CE->guid() );
	corridorRevealSet.push_back( passageCE_E->guid() );
	// All 4 corridor waypoints
	corridorRevealSet.push_back( intWps[6]->guid() );      // WP_CorridorW
	corridorRevealSet.push_back( intWps[10]->guid() );     // WP_CorridorCW
	corridorRevealSet.push_back( intWps[11]->guid() );     // WP_CorridorCE
	corridorRevealSet.push_back( intWps[12]->guid() );     // WP_CorridorE

	for( size_t ci : { size_t( 6 ), size_t( 10 ), size_t( 11 ), size_t( 12 ) } )
		for( auto g : corridorRevealSet )
			if( g != intWps[ci]->guid() )
				intWpReveals[ci]->push_back( g );

	// WP_Bathroom -> BobbyPin
	intWpReveals[4]->push_back( bobbyPin->guid() );

	// WP_Study -> CodePieceB
	intWpReveals[9]->push_back( codePieceB->guid() );

	// WP_Garage -> Door_GarageCorridor, WP_CorridorCE (for garage entry path)
	intWpReveals[5]->push_back( doorGarageCorridor->guid() );
	intWpReveals[5]->push_back( intWps[11]->guid() );     // WP_CorridorCE

	// WP_BedroomA -> Safe
	intWpReveals[7]->push_back( safe->guid() );

	return agent;
}

// ---------------------------------------------------------------------------
// Action Registration
// ---------------------------------------------------------------------------
static void RegisterActions( gie::Planner& planner )
{
	auto sharedHeuristic = []( gie::CalculateHeuristicParams params )
	{
		auto agentEnt = params.simulation.context().entity( params.agent.guid() );
		params.simulation.heuristic.value = EstimateHeistHeuristic( params.simulation, agentEnt );
	};

	// -----------------------------------------------------------------------
	// Observe — stand at a Known waypoint and reveal what it shows (forceLeaf)
	// -----------------------------------------------------------------------
	planner.addLambdaAction( "Observe",
		// evaluate
		[]( gie::EvaluateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			if( !agentEnt ) return false;
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			auto wpSet = params.simulation.tagSet( "Waypoint" );
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			if( !wpSet || !knownSet ) return false;

			for( auto g : *wpSet )
			{
				if( !knownSet->count( g ) ) continue;
				auto wp = ctx.entity( g );
				if( !wp ) continue;
				auto loc = wp->property( "Location" );
				if( !loc ) continue;
				if( glm::distance( agentLoc, *loc->getVec3() ) < 1.f )
				{
					auto reveals = wp->property( "Reveals" );
					if( !reveals ) continue;
					auto revArr = reveals->getGuidArray();
					if( !revArr ) continue;
					for( Guid rg : *revArr )
					{
						if( !knownSet->count( rg ) )
							return true;  // Found unrevealed entity
					}
				}
			}
			return false;
		},
		// simulate — OPAQUE: does NOT reveal entities during planning
		[]( gie::SimulateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			auto explored = agentEnt->property( "ExploredNewArea" );
			if( explored ) explored->value = true;
			SetSimulationCost( params.simulation, 0.f, 2.f );
			return true;
		},
		sharedHeuristic,
		true  // forceLeaf — prevents planner from expanding beyond this
	);

	// -----------------------------------------------------------------------
	// MoveTo — travel to any reachable Known waypoint via the nav graph
	// (portals are waypoints; locked/alarm-gated portals are excluded)
	// -----------------------------------------------------------------------
	planner.addLambdaAction( "MoveTo",
		// evaluate
		[]( gie::EvaluateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			if( !agentEnt ) return false;
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			auto wpSet = params.simulation.tagSet( "Waypoint" );
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			auto portalSet = params.simulation.tagSet( "Portal" );
			if( !wpSet || !knownSet ) return false;

			auto alarm = FindEntityByName( ctx, "AlarmSystem" );
			bool alarmArmed = alarm && *alarm->property( "Armed" )->getBool();

			for( auto g : *wpSet )
			{
				if( !knownSet->count( g ) ) continue;
				auto wp = ctx.entity( g );
				if( !wp ) continue;
				auto loc = wp->property( "Location" );
				if( !loc ) continue;
				if( glm::distance( agentLoc, *loc->getVec3() ) < 1.f ) continue;
				// Skip inaccessible portal waypoints
				if( portalSet && portalSet->count( g ) )
				{
					if( *wp->property( "Locked" )->getBool() ) continue;
					if( *wp->property( "AlarmGated" )->getBool() && alarmArmed ) continue;
					continue; // Don't target portals as destinations (they're just traversed)
				}
				return true;
			}
			return false;
		},
		// simulate
		[]( gie::SimulateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			if( !agentEnt ) return false;
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			auto wpSet = params.simulation.tagSet( "Waypoint" );
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			auto portalSet = params.simulation.tagSet( "Portal" );
			if( !wpSet || !knownSet ) return false;

			auto navGraph = BuildNavGraph( ctx, wpSet, portalSet );

			// Flood-fill from agent's nearest waypoint to find reachable set
			std::set<Guid> navSet( navGraph.begin(), navGraph.end() );
			std::set<Guid> reachable;
			{
				Guid startWP = gie::nearestWaypoint( *params.simulation.world(), navGraph, agentLoc );
				if( startWP != gie::NullGuid )
				{
					std::vector<Guid> frontier = { startWP };
					reachable.insert( startWP );
					while( !frontier.empty() )
					{
						Guid cur = frontier.back(); frontier.pop_back();
						auto ent = ctx.entity( cur );
						if( !ent ) continue;
						auto links = ent->property( "Links" );
						if( !links || !links->getGuidArray() ) continue;
						for( Guid lg : *links->getGuidArray() )
						{
							if( !navSet.count( lg ) ) continue;
							if( reachable.count( lg ) ) continue;
							reachable.insert( lg );
							frontier.push_back( lg );
						}
					}
				}
			}

			// Read visited WP GUIDs to penalize cycling through already-visited waypoints
			auto visitedProp = agentEnt->createProperty( "VisitedWPs", gie::Property::GuidVector{} );
			auto visitedWPs = visitedProp->getGuidArray();

			// Add current nearest WP to visited set
			Guid curWP = gie::nearestWaypoint( *params.simulation.world(), navGraph, agentLoc );
			if( curWP != gie::NullGuid &&
				std::find( visitedWPs->begin(), visitedWPs->end(), curWP ) == visitedWPs->end() )
			{
				visitedWPs->push_back( curWP );
			}

			// Pick best non-portal reachable target
			gie::Entity* bestTarget = nullptr;
			float bestScore = std::numeric_limits<float>::max();

			for( auto g : navGraph )
			{
				// Skip portals as destinations
				if( portalSet && portalSet->count( g ) ) continue;
				if( !reachable.count( g ) ) continue;
				auto ent = ctx.entity( g );
				if( !ent ) continue;
				auto loc = ent->property( "Location" );
				if( !loc ) continue;
				float dist = glm::distance( agentLoc, *loc->getVec3() );
				if( dist < 1.f ) continue;
				float score = dist;
				// Penalize previously-visited waypoints to prevent cycling
				if( std::find( visitedWPs->begin(), visitedWPs->end(), g ) != visitedWPs->end() )
					score += 100.f;
				auto reveals = ent->property( "Reveals" );
				if( reveals )
				{
					auto revArr = reveals->getGuidArray();
					if( revArr )
						for( Guid rg : *revArr )
							if( !knownSet->count( rg ) ) { score -= 50.f; break; }
				}
				if( score < bestScore ) { bestScore = score; bestTarget = ent; }
			}
			if( !bestTarget ) return false;

			// Remember room before move
			Guid roomBefore = *agentEnt->property( "CurrentRoom" )->getGuid();

			float len = MoveAgentAlongPath( params, agentLoc, bestTarget, navGraph );

			// Update CurrentRoom
			glm::vec3 destPos = *bestTarget->property( "Location" )->getVec3();
			auto roomSet = params.simulation.tagSet( "Room" );
			UpdateAgentRoom( ctx, agentEnt, roomSet, destPos );

			// Entering the mansion from outside counts as exploration
			Guid roomAfter = *agentEnt->property( "CurrentRoom" )->getGuid();
			if( roomBefore == gie::NullGuid && roomAfter != gie::NullGuid )
			{
				auto explored = agentEnt->property( "ExploredNewArea" );
				if( explored ) explored->value = true;
			}

			SetSimulationCost( params.simulation, len, 1.f );
			return true;
		},
		sharedHeuristic
	);

	// -----------------------------------------------------------------------
	// UseTool — open the EnergyPanel cover (precondition for Interact)
	// -----------------------------------------------------------------------
	planner.addLambdaAction( "UseTool",
		// evaluate
		[]( gie::EvaluateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			auto panel = FindEntityByName( ctx, "EnergyPanel" );
			if( !panel || !knownSet || !knownSet->count( panel->guid() ) ) return false;
			auto openProp = panel->property( "Open" );
			if( !openProp ) return false;
			return !*openProp->getBool();  // Panel cover must be closed
		},
		// simulate
		[]( gie::SimulateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			auto panel = FindEntityByName( ctx, "EnergyPanel" );
			if( !agentEnt || !panel ) return false;

			auto navGraph = BuildNavGraph( ctx, params.simulation.tagSet( "Waypoint" ), params.simulation.tagSet( "Portal" ) );
			glm::vec3 from = *agentEnt->property( "Location" )->getVec3();
			float len = MoveAgentNearEntity( params, from, panel, navGraph );
			if( len < 0.f ) return false;

			panel->property( "Open" )->value = true;
			SetSimulationCost( params.simulation, len, 2.f );
			return true;
		},
		sharedHeuristic
	);

	// -----------------------------------------------------------------------
	// Interact — interact with a target entity:
	//   Case 1: flip the switch on the opened EnergyPanel to disable alarm
	//   Case 2: open an inspected entity (e.g. safe) whose RequiredItems are all in inventory
	// -----------------------------------------------------------------------
	planner.addLambdaAction( "Interact",
		// evaluate
		[]( gie::EvaluateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();

			// Case 1: EnergyPanel -> disable alarm
			auto panel = FindEntityByName( ctx, "EnergyPanel" );
			if( panel )
			{
				auto openProp = panel->property( "Open" );
				if( openProp && *openProp->getBool() )
				{
					auto alarm = FindEntityByName( ctx, "AlarmSystem" );
					if( alarm && *alarm->property( "Armed" )->getBool() )
						return true;
				}
			}

			// Case 2: generic RequiredItems interaction (non-portal entities like safe)
			auto agent = ctx.entity( params.agent.guid() );
			if( !agent ) return false;
			auto inv = agent->property( "Inventory" )->getGuidArray();
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			if( !knownSet ) return false;
			auto portalSet = params.simulation.tagSet( "Portal" );

			for( auto g : *knownSet )
			{
				if( portalSet && portalSet->count( g ) ) continue;
				auto e = ctx.entity( g ); if( !e ) continue;
				auto inspected = e->property( "Inspected" );
				if( !inspected || !*inspected->getBool() ) continue;
				auto openP = e->property( "Open" );
				if( !openP || *openP->getBool() ) continue;
				auto reqItems = e->property( "RequiredItems" );
				if( !reqItems ) continue;
				auto reqArr = reqItems->getGuidArray();
				if( !reqArr || reqArr->empty() ) continue;

				bool hasAll = true;
				for( Guid reqInfo : *reqArr )
				{
					bool found = false;
					for( Guid itemG : *inv )
					{
						auto item = ctx.entity( itemG ); if( !item ) continue;
						auto info = item->property( "Info" );
						if( info && *info->getGuid() == reqInfo ) { found = true; break; }
					}
					if( !found ) { hasAll = false; break; }
				}
				if( hasAll ) return true;
			}
			return false;
		},
		// simulate
		[]( gie::SimulateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			if( !agentEnt ) return false;

			// Case 1: EnergyPanel -> disable alarm
			auto panel = FindEntityByName( ctx, "EnergyPanel" );
			if( panel )
			{
				auto openProp = panel->property( "Open" );
				if( openProp && *openProp->getBool() )
				{
					auto alarm = FindEntityByName( ctx, "AlarmSystem" );
					if( alarm && *alarm->property( "Armed" )->getBool() )
					{
						auto navGraph = BuildNavGraph( ctx, params.simulation.tagSet( "Waypoint" ), params.simulation.tagSet( "Portal" ) );
						glm::vec3 from = *agentEnt->property( "Location" )->getVec3();
						float len = MoveAgentNearEntity( params, from, panel, navGraph );
						if( len < 0.f ) return false;
						alarm->property( "Armed" )->value = false;
						SetSimulationCost( params.simulation, len, 3.f );
						return true;
					}
				}
			}

			// Case 2: generic RequiredItems interaction
			auto inv = agentEnt->property( "Inventory" )->getGuidArray();
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			if( !knownSet ) return false;
			auto portalSet = params.simulation.tagSet( "Portal" );
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			gie::Entity* bestTarget = nullptr;
			float bestDist = std::numeric_limits<float>::max();
			for( auto g : *knownSet )
			{
				if( portalSet && portalSet->count( g ) ) continue;
				auto e = ctx.entity( g ); if( !e ) continue;
				auto inspected = e->property( "Inspected" );
				if( !inspected || !*inspected->getBool() ) continue;
				auto openP = e->property( "Open" );
				if( !openP || *openP->getBool() ) continue;
				auto reqItems = e->property( "RequiredItems" );
				if( !reqItems ) continue;
				auto reqArr = reqItems->getGuidArray();
				if( !reqArr || reqArr->empty() ) continue;

				bool hasAll = true;
				for( Guid reqInfo : *reqArr )
				{
					bool found = false;
					for( Guid itemG : *inv )
					{
						auto item = ctx.entity( itemG ); if( !item ) continue;
						auto info = item->property( "Info" );
						if( info && *info->getGuid() == reqInfo ) { found = true; break; }
					}
					if( !found ) { hasAll = false; break; }
				}
				if( !hasAll ) continue;
				auto loc = e->property( "Location" ); if( !loc ) continue;
				float d = glm::distance( agentLoc, *loc->getVec3() );
				if( d < bestDist ) { bestDist = d; bestTarget = e; }
			}
			if( !bestTarget ) return false;

			auto navGraph = BuildNavGraph( ctx, params.simulation.tagSet( "Waypoint" ), params.simulation.tagSet( "Portal" ) );
			float len = MoveAgentNearEntity( params, agentLoc, bestTarget, navGraph );
			if( len < 0.f ) return false;

			// Update agent room after movement
			glm::vec3 destPos = *agentEnt->property( "Location" )->getVec3();
			auto roomSet = params.simulation.tagSet( "Room" );
			UpdateAgentRoom( ctx, agentEnt, roomSet, destPos );

			bestTarget->property( "Open" )->value = true;
			SetSimulationCost( params.simulation, len, 3.f );
			return true;
		},
		sharedHeuristic
	);

	// -----------------------------------------------------------------------
	// SearchForItem — pick up nearest Known needed item (filtered by NeededItems)
	// -----------------------------------------------------------------------
	planner.addLambdaAction( "SearchForItem",
		// evaluate
		[]( gie::EvaluateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();
			auto agent = ctx.entity( params.agent.guid() );
			auto inv = agent->property( "Inventory" )->getGuidArray();
			auto neededItems = agent->property( "NeededItems" );
			auto neededArr = neededItems ? neededItems->getGuidArray() : nullptr;
			if( !neededArr || neededArr->empty() ) return false;
			bool agentInside = IsAgentInside( ctx, params.agent.guid() );
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			auto items = ctx.entityTagRegister().tagSet( "Item" );
			if( !items || items->empty() ) return false;

			for( auto g : *items )
			{
				if( knownSet && !knownSet->count( g ) ) continue;
				auto e = ctx.entity( g ); if( !e ) continue;
				if( std::find( inv->begin(), inv->end(), g ) != inv->end() ) continue;
				auto loc = e->property( "Location" ); if( !loc ) continue;
				if( !agentInside && IsPositionInsideHouse( *loc->getVec3() ) ) continue;
				auto info = e->property( "Info" );
				if( !info ) continue;
				if( std::find( neededArr->begin(), neededArr->end(), *info->getGuid() ) == neededArr->end() ) continue;
				return true;
			}
			return false;
		},
		// simulate
		[]( gie::SimulateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();
			auto agent = ctx.entity( params.agent.guid() );
			auto inv = agent->property( "Inventory" )->getGuidArray();
			auto neededItems = agent->property( "NeededItems" );
			auto neededArr = neededItems ? neededItems->getGuidArray() : nullptr;
			if( !neededArr || neededArr->empty() ) return false;
			bool agentInside = IsAgentInside( ctx, params.agent.guid() );
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			auto items = ctx.entityTagRegister().tagSet( "Item" );
			if( !items ) return false;

			auto navGraph = BuildNavGraph( ctx, params.simulation.tagSet( "Waypoint" ), params.simulation.tagSet( "Portal" ) );
			glm::vec3 from = *agent->property( "Location" )->getVec3();

			gie::Entity* best = nullptr; float bestL = std::numeric_limits<float>::max();
			for( auto g : *items )
			{
				if( knownSet && !knownSet->count( g ) ) continue;
				auto e = ctx.entity( g ); if( !e ) continue;
				if( std::find( inv->begin(), inv->end(), g ) != inv->end() ) continue;
				auto loc = e->property( "Location" ); if( !loc ) continue;
				if( !agentInside && IsPositionInsideHouse( *loc->getVec3() ) ) continue;
				auto info = e->property( "Info" );
				if( !info ) continue;
				if( std::find( neededArr->begin(), neededArr->end(), *info->getGuid() ) == neededArr->end() ) continue;
				auto path = gie::getPath( *params.simulation.world(), navGraph, from, *loc->getVec3() );
				float dist = path.length;
				if( path.path.empty() )
					dist = glm::distance( from, *loc->getVec3() );
				if( dist < bestL ) { bestL = dist; best = e; }
			}
			if( !best ) return false;

			float len = MoveAgentAlongPath( params, from, best, navGraph );
			inv->push_back( best->guid() );
			SetSimulationCost( params.simulation, len, 1.f );
			return true;
		},
		sharedHeuristic
	);

	// -----------------------------------------------------------------------
	// Inspect — examine Known entity to discover RequiredItems (forceLeaf)
	// -----------------------------------------------------------------------
	planner.addLambdaAction( "Inspect",
		// evaluate
		[]( gie::EvaluateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();
			auto agent = ctx.entity( params.agent.guid() );
			if( !agent ) return false;
			auto curRoom = agent->property( "CurrentRoom" );
			if( !curRoom || *curRoom->getGuid() == gie::NullGuid ) return false;

			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			if( !knownSet ) return false;
			for( auto g : *knownSet )
			{
				auto e = ctx.entity( g ); if( !e ) continue;
				auto reqItems = e->property( "RequiredItems" );
				if( !reqItems ) continue;
				auto reqArr = reqItems->getGuidArray();
				if( !reqArr || reqArr->empty() ) continue;
				auto inspected = e->property( "Inspected" );
				if( !inspected ) continue;
				if( !*inspected->getBool() ) return true;
			}
			return false;
		},
		// simulate — OPAQUE: only sets ExploredNewArea
		[]( gie::SimulateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();
			auto agent = ctx.entity( params.agent.guid() );
			if( !agent ) return false;
			glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			if( !knownSet ) return false;

			gie::Entity* bestTarget = nullptr;
			float bestDist = std::numeric_limits<float>::max();
			for( auto g : *knownSet )
			{
				auto e = ctx.entity( g ); if( !e ) continue;
				auto reqItems = e->property( "RequiredItems" );
				if( !reqItems ) continue;
				auto reqArr = reqItems->getGuidArray();
				if( !reqArr || reqArr->empty() ) continue;
				auto inspected = e->property( "Inspected" );
				if( !inspected || *inspected->getBool() ) continue;
				auto loc = e->property( "Location" ); if( !loc ) continue;
				float d = glm::distance( agentLoc, *loc->getVec3() );
				if( d < bestDist ) { bestDist = d; bestTarget = e; }
			}
			if( !bestTarget ) return false;

			auto navGraph = BuildNavGraph( ctx, params.simulation.tagSet( "Waypoint" ), params.simulation.tagSet( "Portal" ) );
			float len = MoveAgentNearEntity( params, agentLoc, bestTarget, navGraph );
			if( len < 0.f ) return false;

			agent->property( "ExploredNewArea" )->value = true;
			SetSimulationCost( params.simulation, len, 2.f );
			return true;
		},
		sharedHeuristic,
		true  // forceLeaf
	);

	// -----------------------------------------------------------------------
	// UseItem — unlock an inspected locked entity using inventory items
	// -----------------------------------------------------------------------
	planner.addLambdaAction( "UseItem",
		// evaluate
		[]( gie::EvaluateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();
			auto agent = ctx.entity( params.agent.guid() );
			if( !agent ) return false;
			auto inv = agent->property( "Inventory" )->getGuidArray();
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			if( !knownSet ) return false;

			for( auto g : *knownSet )
			{
				auto e = ctx.entity( g ); if( !e ) continue;
				auto inspected = e->property( "Inspected" );
				if( !inspected || !*inspected->getBool() ) continue;
				auto locked = e->property( "Locked" );
				if( !locked || !*locked->getBool() ) continue;
				auto reqItems = e->property( "RequiredItems" );
				if( !reqItems ) continue;
				auto reqArr = reqItems->getGuidArray();
				if( !reqArr || reqArr->empty() ) continue;

				bool hasAll = true;
				for( Guid reqInfo : *reqArr )
				{
					bool found = false;
					for( Guid itemG : *inv )
					{
						auto item = ctx.entity( itemG ); if( !item ) continue;
						auto info = item->property( "Info" );
						if( info && *info->getGuid() == reqInfo ) { found = true; break; }
					}
					if( !found ) { hasAll = false; break; }
				}
				if( hasAll )
					return true;
			}
			return false;
		},
		// simulate
		[]( gie::SimulateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();
			auto agent = ctx.entity( params.agent.guid() );
			if( !agent ) return false;
			auto inv = agent->property( "Inventory" )->getGuidArray();
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			if( !knownSet ) return false;
			glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();

			gie::Entity* bestTarget = nullptr;
			float bestDist = std::numeric_limits<float>::max();
			for( auto g : *knownSet )
			{
				auto e = ctx.entity( g ); if( !e ) continue;
				auto inspected = e->property( "Inspected" );
				if( !inspected || !*inspected->getBool() ) continue;
				auto locked = e->property( "Locked" );
				if( !locked || !*locked->getBool() ) continue;
				auto reqItems = e->property( "RequiredItems" );
				if( !reqItems ) continue;
				auto reqArr = reqItems->getGuidArray();
				if( !reqArr || reqArr->empty() ) continue;

				bool hasAll = true;
				for( Guid reqInfo : *reqArr )
				{
					bool found = false;
					for( Guid itemG : *inv )
					{
						auto item = ctx.entity( itemG ); if( !item ) continue;
						auto info = item->property( "Info" );
						if( info && *info->getGuid() == reqInfo ) { found = true; break; }
					}
					if( !found ) { hasAll = false; break; }
				}
				if( !hasAll ) continue;
				auto loc = e->property( "Location" ); if( !loc ) continue;
				float d = glm::distance( agentLoc, *loc->getVec3() );
				if( d < bestDist ) { bestDist = d; bestTarget = e; }
			}
			if( !bestTarget ) return false;

			auto navGraph = BuildNavGraph( ctx, params.simulation.tagSet( "Waypoint" ), params.simulation.tagSet( "Portal" ) );
			float len = MoveAgentNearEntity( params, agentLoc, bestTarget, navGraph );
			if( len < 0.f ) return false;

			bestTarget->property( "Locked" )->value = false;
			SetSimulationCost( params.simulation, len, 2.f );
			return true;
		},
		sharedHeuristic
	);

	// -----------------------------------------------------------------------
	// BruteForceSafe — crowbar + safe room, extremely high cost
	// -----------------------------------------------------------------------
	planner.addLambdaAction( "BruteForceSafe",
		// evaluate
		[]( gie::EvaluateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();
			if( !IsAgentInSafeRoom( ctx, params.agent.guid() ) ) return false;
			const gie::Entity* safe = FindEntityByName( ctx, "Safe" );
			if( !safe ) return false;
			if( *const_cast< gie::Entity* >( safe )->property( "Open" )->getBool() ) return false;
			auto agent = ctx.entity( params.agent.guid() );
			auto inv = agent->property( "Inventory" )->getGuidArray();
			for( Guid it : *inv )
			{
				auto e = ctx.entity( it );
				if( !e ) continue;
				auto info = e->property( "Info" ); if( !info ) continue;
				auto infoEnt = ctx.entity( *info->getGuid() );
				if( infoEnt && gie::stringRegister().get( infoEnt->nameHash() ) == std::string_view( "CrowbarInfo" ) )
					return true;
			}
			return false;
		},
		// simulate
		[]( gie::SimulateParams params ) -> bool
		{
			auto& ctx = params.simulation.context();
			gie::Entity* safe = FindEntityByName( ctx, "Safe" );
			if( !safe ) return false;
			safe->property( "Open" )->value = true;
			SetSimulationCost( params.simulation, 0.f, 200.f );
			return true;
		},
		sharedHeuristic
	);
}

// ---------------------------------------------------------------------------
// Action Execution — applies planned actions to the real world
// ---------------------------------------------------------------------------
static std::string ExecuteObserve( gie::World& world, gie::Agent* agent )
{
	glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();
	auto wpSet = world.context().entityTagRegister().tagSet( "Waypoint" );
	auto knownSet = world.context().entityTagRegister().tagSet( "Known" );
	std::string wpName = "?";
	std::vector<std::string> revealed;

	if( wpSet && knownSet )
	{
		for( auto g : *wpSet )
		{
			if( !knownSet->count( g ) ) continue;
			auto wp = world.entity( g );
			if( !wp ) continue;
			auto loc = wp->property( "Location" );
			if( !loc ) continue;
			if( glm::distance( agentLoc, *loc->getVec3() ) < 1.f )
			{
				wpName = gie::stringRegister().get( wp->nameHash() );
				auto reveals = wp->property( "Reveals" );
				if( reveals )
				{
					auto revArr = reveals->getGuidArray();
					if( revArr )
					{
						for( Guid rg : *revArr )
						{
							auto ent = world.entity( rg );
							if( ent )
							{
								if( !knownSet->count( rg ) )
									revealed.push_back( std::string( gie::stringRegister().get( ent->nameHash() ) ) );
								world.context().entityTagRegister().tag( ent, { H( "Known" ) } );
							}
						}
					}
				}
			}
		}
	}
	// Auto-open adjacent unlocked portals (first interaction when unlocked)
	auto portalSet = world.context().entityTagRegister().tagSet( "Portal" );
	// Re-fetch knownSet since we just tagged new entities
	knownSet = world.context().entityTagRegister().tagSet( "Known" );
	if( portalSet && knownSet )
	{
		for( auto pg : *portalSet )
		{
			if( !knownSet->count( pg ) ) continue;
			auto portal = world.entity( pg ); if( !portal ) continue;
			auto openProp = portal->property( "Open" );
			if( !openProp || *openProp->getBool() ) continue; // already open
			if( *portal->property( "Locked" )->getBool() ) continue; // still locked
			auto alarmGatedProp = portal->property( "AlarmGated" );
			auto alarm = FindEntityByName( world.context(), "AlarmSystem" );
			if( alarmGatedProp && *alarmGatedProp->getBool() && alarm && *alarm->property( "Armed" )->getBool() ) continue;
			// Check if agent is near one of the portal's sides
			auto portalLoc = portal->property( "Location" );
			if( portalLoc && glm::distance( agentLoc, *portalLoc->getVec3() ) < 15.f )
			{
				openProp->value = true;
				revealed.push_back( std::string( gie::stringRegister().get( portal->nameHash() ) ) + " (opened)" );
			}
		}
	}

	agent->property( "ExploredNewArea" )->value = true;
	agent->property( "DiscoveryCount" )->value = static_cast<float>( CountKnown( world.context() ) );

	std::string detail = "at " + wpName;
	if( !revealed.empty() )
	{
		detail += " -> discovered ";
		for( size_t i = 0; i < revealed.size(); i++ )
		{
			if( i > 0 ) detail += ", ";
			detail += revealed[i];
		}
	}
	return detail;
}

static std::string ExecuteMoveTo( gie::World& world, gie::Agent* agent )
{
	glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();
	auto wpSet = world.context().entityTagRegister().tagSet( "Waypoint" );
	auto portalSet = world.context().entityTagRegister().tagSet( "Portal" );
	auto knownSet = world.context().entityTagRegister().tagSet( "Known" );
	if( !knownSet || !wpSet ) return "";

	// Build nav graph: Known waypoints, excluding locked/alarm-gated portals
	auto navGraph = BuildNavGraph( world.context(), wpSet, portalSet );

	// Flood-fill from agent's nearest waypoint to find reachable set
	std::set<Guid> navSet( navGraph.begin(), navGraph.end() );
	std::set<Guid> reachable;
	{
		Guid startWP = gie::nearestWaypoint( world, navGraph, agentLoc );
		if( startWP != gie::NullGuid )
		{
			std::vector<Guid> frontier = { startWP };
			reachable.insert( startWP );
			while( !frontier.empty() )
			{
				Guid cur = frontier.back(); frontier.pop_back();
				auto ent = world.entity( cur );
				if( !ent ) continue;
				auto links = ent->property( "Links" );
				if( !links || !links->getGuidArray() ) continue;
				for( Guid lg : *links->getGuidArray() )
				{
					if( !navSet.count( lg ) ) continue;
					if( reachable.count( lg ) ) continue;
					reachable.insert( lg );
					frontier.push_back( lg );
				}
			}
		}
	}

	// Pick best non-portal reachable target (same scoring: prefer unknown reveals)
	gie::Entity* bestTarget = nullptr;
	float bestScore = std::numeric_limits<float>::max();
	for( auto g : navGraph )
	{
		// Skip portals as destinations
		if( portalSet && portalSet->count( g ) ) continue;
		if( !reachable.count( g ) ) continue;
		auto wp = world.entity( g );
		if( !wp ) continue;
		auto loc = wp->property( "Location" );
		if( !loc ) continue;
		float dist = glm::distance( agentLoc, *loc->getVec3() );
		if( dist < 1.f ) continue;
		float score = dist;
		auto reveals = wp->property( "Reveals" );
		if( reveals )
		{
			auto revArr = reveals->getGuidArray();
			if( revArr )
				for( Guid rg : *revArr )
					if( !knownSet->count( rg ) ) { score -= 50.f; break; }
		}
		if( score < bestScore ) { bestScore = score; bestTarget = wp; }
	}

	g_LastActionPath.clear();
	if( !bestTarget ) return "";

	// Pathfind from agent to target through nav graph
	glm::vec3 targetLoc = *bestTarget->property( "Location" )->getVec3();
	auto pathResult = gie::getPath( world, navGraph, agentLoc, targetLoc );

	// Collect positions along the path for visualization
	std::string route;
	if( !pathResult.path.empty() )
	{
		g_LastActionPath.push_back( agentLoc );
		for( auto wpGuid : pathResult.path )
		{
			auto wpEnt = world.entity( wpGuid );
			if( wpEnt )
			{
				g_LastActionPath.push_back( *wpEnt->property( "Location" )->getVec3() );
				if( !route.empty() ) route += " -> ";
				route += std::string( gie::stringRegister().get( wpEnt->nameHash() ) );
			}
		}
		// Auto-open any closed unlocked portals along the path
		AutoOpenPortalsOnPath( world.context(), portalSet, pathResult.path );
	}

	// Teleport agent to destination
	*agent->property( "Location" )->getVec3() = targetLoc;

	// Update CurrentRoom based on destination
	auto roomSet = world.context().entityTagRegister().tagSet( "Room" );
	UpdateAgentRoom( world.context(), agent, roomSet, targetLoc );

	std::string targetName = std::string( gie::stringRegister().get( bestTarget->nameHash() ) );
	if( route.empty() )
		return "-> " + targetName;
	return "-> " + targetName + " [" + route + "]";
}

// Move agent to nearest linked waypoint of target, recording path. Returns WP name.
static std::string ExecuteMoveNearEntity( gie::World& world, gie::Agent* agent, gie::Entity* target )
{
	if( !target ) return "";
	glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();
	auto wpSet = world.context().entityTagRegister().tagSet( "Waypoint" );
	auto portalSet = world.context().entityTagRegister().tagSet( "Portal" );
	auto navGraph = BuildNavGraph( world.context(), wpSet, portalSet );
	Guid wpGuid = FindNearestLinkedWP( world.context(), target, navGraph, agentLoc );
	if( wpGuid == gie::NullGuid ) return "";
	auto wpEnt = world.entity( wpGuid );
	if( !wpEnt ) return "";

	glm::vec3 wpLoc = *wpEnt->property( "Location" )->getVec3();
	auto path = gie::getPath( world, navGraph, agentLoc, wpLoc );
	g_LastActionPath.clear();
	g_LastActionPath.push_back( agentLoc );
	for( auto& wpG : path.path )
	{
		auto we = world.entity( wpG );
		if( we ) g_LastActionPath.push_back( *we->property( "Location" )->getVec3() );
	}
	if( g_LastActionPath.size() == 1 )
		g_LastActionPath.push_back( wpLoc );
	*agent->property( "Location" )->getVec3() = wpLoc;

	auto roomSet = world.context().entityTagRegister().tagSet( "Room" );
	UpdateAgentRoom( world.context(), agent, roomSet, wpLoc );

	AutoOpenPortalsOnPath( world.context(), portalSet, navGraph );
	return std::string( gie::stringRegister().get( wpEnt->nameHash() ) );
}

static std::string ExecuteUseTool( gie::World& world, gie::Agent* agent )
{
	auto panel = FindEntityByName( world.context(), "EnergyPanel" );
	if( !panel ) return "";
	ExecuteMoveNearEntity( world, agent, panel );
	panel->property( "Open" )->value = true;
	return "opened EnergyPanel cover";
}

static std::string ExecuteInteract( gie::World& world, gie::Agent* agent )
{
	// Case 1: EnergyPanel -> disable alarm
	auto panel = FindEntityByName( world.context(), "EnergyPanel" );
	if( panel )
	{
		auto openProp = panel->property( "Open" );
		if( openProp && *openProp->getBool() )
		{
			auto alarm = FindEntityByName( world.context(), "AlarmSystem" );
			if( alarm && *alarm->property( "Armed" )->getBool() )
			{
				ExecuteMoveNearEntity( world, agent, panel );
				alarm->property( "Armed" )->value = false;
				return "disabled alarm via EnergyPanel";
			}
		}
	}

	// Case 2: generic RequiredItems interaction
	auto knownSet = world.context().entityTagRegister().tagSet( "Known" );
	if( !knownSet ) return "";
	auto inv = agent->property( "Inventory" )->getGuidArray();
	auto portalSet = world.context().entityTagRegister().tagSet( "Portal" );
	glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();

	gie::Entity* bestTarget = nullptr;
	float bestDist = std::numeric_limits<float>::max();
	for( auto g : *knownSet )
	{
		if( portalSet && portalSet->count( g ) ) continue;
		auto e = world.entity( g ); if( !e ) continue;
		auto inspected = e->property( "Inspected" );
		if( !inspected || !*inspected->getBool() ) continue;
		auto openP = e->property( "Open" );
		if( !openP || *openP->getBool() ) continue;
		auto reqItems = e->property( "RequiredItems" );
		if( !reqItems ) continue;
		auto reqArr = reqItems->getGuidArray();
		if( !reqArr || reqArr->empty() ) continue;

		bool hasAll = true;
		for( Guid reqInfo : *reqArr )
		{
			bool found = false;
			for( Guid itemG : *inv )
			{
				auto item = world.entity( itemG ); if( !item ) continue;
				auto info = item->property( "Info" );
				if( info && *info->getGuid() == reqInfo ) { found = true; break; }
			}
			if( !found ) { hasAll = false; break; }
		}
		if( !hasAll ) continue;
		auto loc = e->property( "Location" ); if( !loc ) continue;
		float d = glm::distance( agentLoc, *loc->getVec3() );
		if( d < bestDist ) { bestDist = d; bestTarget = e; }
	}
	if( !bestTarget ) return "";

	std::string name( gie::stringRegister().get( bestTarget->nameHash() ) );
	ExecuteMoveNearEntity( world, agent, bestTarget );
	bestTarget->property( "Open" )->value = true;
	return "opened " + name;
}

static std::string ExecuteSearchForItem( gie::World& world, gie::Agent* agent )
{
	glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();
	auto inv = agent->property( "Inventory" )->getGuidArray();
	auto neededItems = agent->property( "NeededItems" );
	auto neededArr = neededItems ? neededItems->getGuidArray() : nullptr;
	if( !neededArr || neededArr->empty() ) return "";
	auto knownSet = world.context().entityTagRegister().tagSet( "Known" );
	auto itemSet = world.context().entityTagRegister().tagSet( "Item" );
	if( !itemSet ) return "";
	bool agentInside = IsAgentInside( world.context(), agent->guid() );

	gie::Entity* best = nullptr;
	float bestDist = std::numeric_limits<float>::max();
	for( auto g : *itemSet )
	{
		if( knownSet && !knownSet->count( g ) ) continue;
		if( std::find( inv->begin(), inv->end(), g ) != inv->end() ) continue;
		auto e = world.entity( g ); if( !e ) continue;
		auto loc = e->property( "Location" ); if( !loc ) continue;
		if( !agentInside && IsPositionInsideHouse( *loc->getVec3() ) ) continue;
		auto info = e->property( "Info" );
		if( !info ) continue;
		if( std::find( neededArr->begin(), neededArr->end(), *info->getGuid() ) == neededArr->end() ) continue;
		float dist = glm::distance( agentLoc, *loc->getVec3() );
		if( dist < bestDist ) { bestDist = dist; best = e; }
	}
	if( !best ) return "";
	glm::vec3 itemLoc = *best->property( "Location" )->getVec3();
	auto wpSet2 = world.context().entityTagRegister().tagSet( "Waypoint" );
	auto portalSet = world.context().entityTagRegister().tagSet( "Portal" );
	auto navGraph2 = BuildNavGraph( world.context(), wpSet2, portalSet );
	Guid nearWP = FindNearestLinkedWP( world.context(), best, navGraph2, agentLoc );
	g_LastActionPath.clear();
	g_LastActionPath.push_back( agentLoc );
	if( nearWP != gie::NullGuid )
	{
		auto wpEnt = world.entity( nearWP );
		if( wpEnt )
		{
			glm::vec3 wpLoc = *wpEnt->property( "Location" )->getVec3();
			auto path = gie::getPath( world, navGraph2, agentLoc, wpLoc );
			for( auto& wpG : path.path )
			{
				auto we = world.entity( wpG );
				if( we ) g_LastActionPath.push_back( *we->property( "Location" )->getVec3() );
			}
			AutoOpenPortalsOnPath( world.context(), portalSet, path.path );
		}
	}
	g_LastActionPath.push_back( itemLoc );
	*agent->property( "Location" )->getVec3() = itemLoc;
	inv->push_back( best->guid() );
	auto roomSet = world.context().entityTagRegister().tagSet( "Room" );
	UpdateAgentRoom( world.context(), agent, roomSet, itemLoc );
	return std::string( gie::stringRegister().get( best->nameHash() ) );
}

static std::string ExecuteInspect( gie::World& world, gie::Agent* agent )
{
	auto knownSet = world.context().entityTagRegister().tagSet( "Known" );
	if( !knownSet ) return "";
	glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();

	// Find nearest inspectable entity
	gie::Entity* bestTarget = nullptr;
	float bestDist = std::numeric_limits<float>::max();
	for( auto g : *knownSet )
	{
		auto e = world.entity( g ); if( !e ) continue;
		auto reqItems = e->property( "RequiredItems" );
		if( !reqItems ) continue;
		auto reqArr = reqItems->getGuidArray();
		if( !reqArr || reqArr->empty() ) continue;
		auto inspected = e->property( "Inspected" );
		if( !inspected || *inspected->getBool() ) continue;
		auto loc = e->property( "Location" ); if( !loc ) continue;
		float d = glm::distance( agentLoc, *loc->getVec3() );
		if( d < bestDist ) { bestDist = d; bestTarget = e; }
	}
	if( !bestTarget ) return "";

	std::string name( gie::stringRegister().get( bestTarget->nameHash() ) );

	// Move agent to nearest linked waypoint of target
	ExecuteMoveNearEntity( world, agent, bestTarget );

	// Set Inspected = true
	bestTarget->property( "Inspected" )->value = true;

	// Append RequiredItems to agent.NeededItems (deduplicating)
	auto reqArr = bestTarget->property( "RequiredItems" )->getGuidArray();
	auto neededArr = agent->property( "NeededItems" )->getGuidArray();
	for( Guid reqInfo : *reqArr )
	{
		if( std::find( neededArr->begin(), neededArr->end(), reqInfo ) == neededArr->end() )
			neededArr->push_back( reqInfo );
	}

	// If entity is a Portal, tag any unknown-side waypoints as Known
	auto portalSet = world.context().entityTagRegister().tagSet( "Portal" );
	if( portalSet && portalSet->count( bestTarget->guid() ) )
	{
		Guid sA = *bestTarget->property( "SideA" )->getGuid();
		Guid sB = *bestTarget->property( "SideB" )->getGuid();
		auto knownSetW = world.context().entityTagRegister().tagSet( "Known" );
		Guid sides[] = { sA, sB };
		for( Guid side : sides )
		{
			if( knownSetW && knownSetW->count( side ) ) continue;
			auto sideEnt = world.entity( side );
			if( sideEnt )
				world.context().entityTagRegister().tag( sideEnt, { H( "Known" ) } );
		}
	}

	agent->property( "ExploredNewArea" )->value = true;
	agent->property( "DiscoveryCount" )->value = static_cast<float>( CountKnown( world.context() ) );
	return "inspected " + name;
}

static std::string ExecuteUseItem( gie::World& world, gie::Agent* agent )
{
	auto knownSet = world.context().entityTagRegister().tagSet( "Known" );
	if( !knownSet ) return "";
	auto inv = agent->property( "Inventory" )->getGuidArray();
	glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();

	gie::Entity* bestTarget = nullptr;
	float bestDist = std::numeric_limits<float>::max();
	for( auto g : *knownSet )
	{
		auto e = world.entity( g ); if( !e ) continue;
		auto inspected = e->property( "Inspected" );
		if( !inspected || !*inspected->getBool() ) continue;
		auto locked = e->property( "Locked" );
		if( !locked || !*locked->getBool() ) continue;
		auto reqItems = e->property( "RequiredItems" );
		if( !reqItems ) continue;
		auto reqArr = reqItems->getGuidArray();
		if( !reqArr || reqArr->empty() ) continue;

		bool hasAll = true;
		for( Guid reqInfo : *reqArr )
		{
			bool found = false;
			for( Guid itemG : *inv )
			{
				auto item = world.entity( itemG ); if( !item ) continue;
				auto info = item->property( "Info" );
				if( info && *info->getGuid() == reqInfo ) { found = true; break; }
			}
			if( !found ) { hasAll = false; break; }
		}
		if( !hasAll ) continue;
		auto loc = e->property( "Location" ); if( !loc ) continue;
		float d = glm::distance( agentLoc, *loc->getVec3() );
		if( d < bestDist ) { bestDist = d; bestTarget = e; }
	}
	if( !bestTarget ) return "";

	std::string name( gie::stringRegister().get( bestTarget->nameHash() ) );
	ExecuteMoveNearEntity( world, agent, bestTarget );
	bestTarget->property( "Locked" )->value = false;
	return "unlocked " + name;
}

static std::string ExecuteBruteForceSafe( gie::World& world, gie::Agent* agent )
{
	auto safe = FindEntityByName( world.context(), "Safe" );
	if( !safe ) return "";
	safe->property( "Open" )->value = true;
	return "safe forced open with crowbar";
}

static std::string ExecuteAction( gie::World& world, gie::Agent* agent, const std::string& actionName )
{
	if( actionName == "Observe" )           return ExecuteObserve( world, agent );
	else if( actionName == "MoveTo" )       return ExecuteMoveTo( world, agent );
	else if( actionName == "UseTool" )      return ExecuteUseTool( world, agent );
	else if( actionName == "Interact" )     return ExecuteInteract( world, agent );
	else if( actionName == "SearchForItem" )return ExecuteSearchForItem( world, agent );
	else if( actionName == "Inspect" )      return ExecuteInspect( world, agent );
	else if( actionName == "UseItem" )      return ExecuteUseItem( world, agent );
	else if( actionName == "BruteForceSafe" )   return ExecuteBruteForceSafe( world, agent );
	return "";
}

// ---------------------------------------------------------------------------
// Gameplay loop — single cycle and full loop
// ---------------------------------------------------------------------------
// CycleResult is defined in gameplay_common.h

static bool g_StorePlanners = true; // false during validation (planners outlive their worlds)

static CycleResult RunGameplayCycle( gie::World& world, gie::Agent* agent, bool useHeuristics = true )
{
	auto getInventoryNames = [&]() -> std::vector<std::string>
	{
		std::vector<std::string> names;
		auto inv = agent->property( "Inventory" )->getGuidArray();
		if( inv )
			for( auto g : *inv )
				if( auto e = world.entity( g ) )
					names.push_back( std::string( gie::stringRegister().get( e->nameHash() ) ) );
		return names;
	};

	auto safe = FindEntityByName( world.context(), "Safe" );
	if( safe && *safe->property( "Open" )->getBool() )
	{
		g_GameplayLog.primaryGoalReached = true;
		return CycleResult::GoalReached;
	}

	Guid safeOpenPropGuid = safe ? safe->property( "Open" )->guid() : gie::NullGuid;
	Guid exploredPropGuid = agent->property( "ExploredNewArea" )->guid();

	int cycleNum = static_cast<int>( g_GameplayLog.cycles.size() ) + 1;
	GameplayCycleEntry entry;
	entry.cycle = cycleNum;
	entry.agentPosBefore = *agent->property( "Location" )->getVec3();
	entry.knownCountBefore = CountKnown( world.context() );

	// --- Try primary goal: open safe ---
	{
		gie::Goal primaryGoal{ world };
		if( safeOpenPropGuid != gie::NullGuid )
			primaryGoal.targets.emplace_back( safeOpenPropGuid, true );

		gie::Planner primaryPlanner{};
		RegisterActions( primaryPlanner );
		primaryPlanner.depthLimitMutator() = useHeuristics ? 12 : 14;
		primaryPlanner.simulate( primaryGoal, *agent );
		primaryPlanner.plan( useHeuristics );

		auto& planned = primaryPlanner.planActions();
		if( !planned.empty() )
		{
			entry.goalType = "Primary";
			entry.planFound = true;
			entry.simulationCount = primaryPlanner.simulations().size();
			for( int i = static_cast<int>( planned.size() ) - 1; i >= 0; i-- )
				entry.actionNames.push_back( std::string( planned[i]->name() ) );

			std::vector<std::vector<glm::vec3>> cyclePaths;
			for( auto& name : entry.actionNames )
			{
				g_LastActionPath.clear();
				entry.actionDetails.push_back( ExecuteAction( world, agent, name ) );
				cyclePaths.push_back( std::move( g_LastActionPath ) );
			}

			entry.agentPosAfter = *agent->property( "Location" )->getVec3();
			entry.knownCountAfter = CountKnown( world.context() );
			entry.inventoryAfter = getInventoryNames();
			if( g_StorePlanners )
				entry.plannerPtr = std::make_unique<gie::Planner>( std::move( primaryPlanner ) );
			g_GameplayLog.agentTrail.push_back( entry.agentPosAfter );
			g_GameplayLog.cycles.push_back( std::move( entry ) );
			g_CycleActionPaths.push_back( std::move( cyclePaths ) );

			safe = FindEntityByName( world.context(), "Safe" );
			if( safe && *safe->property( "Open" )->getBool() )
			{
				g_GameplayLog.primaryGoalReached = true;
				return CycleResult::GoalReached;
			}
			return CycleResult::Continued;
		}
	}

	// --- Primary unreachable, try exploration goal ---
	agent->property( "ExploredNewArea" )->value = false;

	{
		gie::Goal exploreGoal{ world };
		exploreGoal.targets.emplace_back( exploredPropGuid, true );

		gie::Planner explorePlanner{};
		RegisterActions( explorePlanner );
		explorePlanner.depthLimitMutator() = 4;
		explorePlanner.logStepsMutator() = false;
		explorePlanner.simulate( exploreGoal, *agent );
		explorePlanner.plan( useHeuristics );

		auto& planned = explorePlanner.planActions();
		if( !planned.empty() )
		{
			entry.goalType = "Explore";
			entry.planFound = true;
			entry.simulationCount = explorePlanner.simulations().size();
			for( int i = static_cast<int>( planned.size() ) - 1; i >= 0; i-- )
				entry.actionNames.push_back( std::string( planned[i]->name() ) );

			std::vector<std::vector<glm::vec3>> cyclePaths;
			for( auto& name : entry.actionNames )
			{
				g_LastActionPath.clear();
				entry.actionDetails.push_back( ExecuteAction( world, agent, name ) );
				cyclePaths.push_back( std::move( g_LastActionPath ) );
			}

			entry.agentPosAfter = *agent->property( "Location" )->getVec3();
			entry.knownCountAfter = CountKnown( world.context() );
			entry.inventoryAfter = getInventoryNames();
			if( g_StorePlanners )
				entry.plannerPtr = std::make_unique<gie::Planner>( std::move( explorePlanner ) );
			g_GameplayLog.agentTrail.push_back( entry.agentPosAfter );
			g_GameplayLog.cycles.push_back( std::move( entry ) );
			g_CycleActionPaths.push_back( std::move( cyclePaths ) );
			return CycleResult::Continued;
		}
	}

	// --- Stuck: neither goal produced a plan ---
	entry.goalType = "Stuck";
	entry.planFound = false;
	entry.agentPosAfter = entry.agentPosBefore;
	entry.knownCountAfter = entry.knownCountBefore;
	entry.inventoryAfter = getInventoryNames();
	g_GameplayLog.cycles.push_back( std::move( entry ) );
	return CycleResult::Stuck;
}

static void RunGameplayLoop( gie::World& world, gie::Agent* agent, gie::Planner& planner, bool useHeuristics = true )
{
	g_GameplayLog = {};
	g_CycleActionPaths.clear();
	g_LastActionPath.clear();
	g_GameplayLog.started = true;
	g_GameplayLog.agentTrail.push_back( *agent->property( "Location" )->getVec3() );

	const int maxCycles = 30;
	for( int i = 0; i < maxCycles; i++ )
	{
		CycleResult result = RunGameplayCycle( world, agent, useHeuristics );
		if( result != CycleResult::Continued )
			break;
	}
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int heistOpenSafe( ExampleParameters& params )
{
	gie::Agent* agent = heistOpenSafe_world( params );

	// Register actions on the main planner (for visualization Plan! button)
	RegisterActions( params.planner );
	params.planner.depthLimitMutator() = 20;

	params.planner.simulate( params.goal, *agent );

	// Run the gameplay loop (deferred in visualization mode — triggered by GUI button)
	if( !params.visualize )
	{
		RunGameplayLoop( params.world, agent, params.planner );
	}

	return 0;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
int heistOpenSafeValidateResult( std::string& failMsg )
{
	// Helper: extract flat action sequence and total simulation count from g_GameplayLog
	auto captureResults = []() -> std::pair<std::vector<std::string>, size_t>
	{
		std::vector<std::string> actions;
		size_t totalSims = 0;
		for( auto& c : g_GameplayLog.cycles )
		{
			for( auto& a : c.actionNames )
				actions.push_back( a );
			totalSims += c.simulationCount;
		}
		return { actions, totalSims };
	};

	// Skip planner storage during validation — planners would outlive their worlds
	g_StorePlanners = false;

	// --- Run with heuristics (A*) ---
	gie::World world{};
	gie::Planner planner{};
	gie::Goal goal{ world };
	ExampleParameters params{ world, planner, goal };

	VALIDATE( heistOpenSafe( params ) == 0, "heistOpenSafe() setup failed (heuristic)" );

	// Debug: print all cycles
	printf( "  Gameplay cycles: %zu, goalReached: %s\n", g_GameplayLog.cycles.size(), g_GameplayLog.primaryGoalReached ? "yes" : "no" );
	for( size_t ci = 0; ci < g_GameplayLog.cycles.size(); ci++ )
	{
		auto& c = g_GameplayLog.cycles[ci];
		printf( "  Cycle %d [%s] sims=%zu actions=%zu pos=(%.1f,%.1f)->(%.1f,%.1f) known=%d->%d\n",
			c.cycle, c.goalType.c_str(), c.simulationCount, c.actionNames.size(),
			c.agentPosBefore.x, c.agentPosBefore.y, c.agentPosAfter.x, c.agentPosAfter.y,
			c.knownCountBefore, c.knownCountAfter );
		for( size_t ai = 0; ai < c.actionNames.size(); ai++ )
			printf( "    %zu. %s — %s\n", ai + 1, c.actionNames[ai].c_str(),
				ai < c.actionDetails.size() ? c.actionDetails[ai].c_str() : "" );
	}

	VALIDATE( g_GameplayLog.primaryGoalReached, "primary goal should be reached (heuristic)" );
	VALIDATE( g_GameplayLog.cycles.size() >= 3, "should have at least 3 planning cycles (heuristic)" );

	bool hasExplore = false;
	for( auto& c : g_GameplayLog.cycles )
		if( c.goalType == "Explore" ) { hasExplore = true; break; }
	VALIDATE( hasExplore, "should have at least one Explore cycle" );

	bool hasOpenSafe = false;
	for( auto& c : g_GameplayLog.cycles )
		for( auto& a : c.actionNames )
			if( a == "Interact" || a == "BruteForceSafe" ) { hasOpenSafe = true; break; }
	VALIDATE( hasOpenSafe, "plan should include Interact or BruteForceSafe" );

	auto safe = FindEntityByName( world.context(), "Safe" );
	VALIDATE( safe != nullptr, "safe should exist" );
	VALIDATE( *safe->property( "Open" )->getBool(), "safe should be open (heuristic)" );

	auto [heuristicActions, heuristicSims] = captureResults();

	// --- Run without heuristics (BFS) ---
	gie::World world2{};
	gie::Planner planner2{};
	gie::Goal goal2{ world2 };
	ExampleParameters params2{ world2, planner2, goal2 };

	gie::Agent* agent2 = heistOpenSafe_world( params2 );
	RegisterActions( params2.planner );
	params2.planner.depthLimitMutator() = 20;
	params2.planner.simulate( params2.goal, *agent2 );
	RunGameplayLoop( params2.world, agent2, params2.planner, false );

	// Debug: print BFS cycles
	printf( "  BFS cycles: %zu, goalReached: %s\n", g_GameplayLog.cycles.size(), g_GameplayLog.primaryGoalReached ? "yes" : "no" );
	for( size_t ci = 0; ci < g_GameplayLog.cycles.size(); ci++ )
	{
		auto& c = g_GameplayLog.cycles[ci];
		printf( "  BFS Cycle %d [%s] sims=%zu actions=%zu pos=(%.1f,%.1f)->(%.1f,%.1f) known=%d->%d\n",
			c.cycle, c.goalType.c_str(), c.simulationCount, c.actionNames.size(),
			c.agentPosBefore.x, c.agentPosBefore.y, c.agentPosAfter.x, c.agentPosAfter.y,
			c.knownCountBefore, c.knownCountAfter );
		for( size_t ai = 0; ai < c.actionNames.size(); ai++ )
			printf( "    %zu. %s — %s\n", ai + 1, c.actionNames[ai].c_str(),
				ai < c.actionDetails.size() ? c.actionDetails[ai].c_str() : "" );
	}

	VALIDATE( g_GameplayLog.primaryGoalReached, "primary goal should be reached (BFS)" );

	auto [bfsActions, bfsSims] = captureResults();

	// Both should end with a safe-opening action
	VALIDATE( !bfsActions.empty() && ( bfsActions.back() == "Interact" || bfsActions.back() == "BruteForceSafe" ),
		"BFS plan should end with safe-opening action" );
	VALIDATE( !heuristicActions.empty() && ( heuristicActions.back() == "Interact" || heuristicActions.back() == "BruteForceSafe" ),
		"A* plan should end with safe-opening action" );

	printf( "  simulations: A*=%zu BFS=%zu\n", heuristicSims, bfsSims );

	g_GameplayLog = {};
	g_CycleActionPaths.clear();
	g_StorePlanners = true;

	return 0;
}

// ---------------------------------------------------------------------------
// GL Draw — gameplay character trail and known entity markers
// ---------------------------------------------------------------------------
static void GLDrawFunc6( gie::World& world, gie::Planner& planner )
{
	glm::vec3 offset = -g_DrawingLimits.center;
	glm::vec3 scale = g_DrawingLimits.scale;

	// Build detailed trail from action paths (follows waypoint connections)
	std::vector<glm::vec3> detailedTrail;
	if( !g_GameplayLog.agentTrail.empty() )
		detailedTrail.push_back( g_GameplayLog.agentTrail[0] ); // starting position

	for( size_t ci = 0; ci < g_CycleActionPaths.size() && ci < g_GameplayLog.cycles.size(); ci++ )
	{
		auto& cyclePaths = g_CycleActionPaths[ci];
		for( size_t ai = 0; ai < cyclePaths.size(); ai++ )
		{
			auto& path = cyclePaths[ai];
			if( path.size() >= 2 )
			{
				// Add all intermediate + final points from the pathfinding result
				for( size_t pi = 1; pi < path.size(); pi++ )
					detailedTrail.push_back( path[pi] );
			}
			else
			{
				// Non-movement action — just record the cycle's final position
				// (only add if this is the last action in the cycle to avoid duplicates)
				if( ai == cyclePaths.size() - 1 )
					detailedTrail.push_back( g_GameplayLog.cycles[ci].agentPosAfter );
			}
		}
		// If cycle has no path data at all, use the endpoint
		if( cyclePaths.empty() )
			detailedTrail.push_back( g_GameplayLog.cycles[ci].agentPosAfter );
	}

	// Draw agent trail (follows waypoint links)
	if( detailedTrail.size() > 1 )
	{
		glLineWidth( 2.0f );
		glColor3f( 0.2f, 0.8f, 0.2f );
		glBegin( GL_LINE_STRIP );
		for( auto& pos : detailedTrail )
		{
			glm::vec3 p = ( pos + offset ) * scale;
			glVertex3f( p.x, p.y, p.z );
		}
		glEnd();
		glLineWidth( 1.0f );
	}

	// Draw breadcrumb dots at cycle endpoints
	if( g_GameplayLog.agentTrail.size() > 1 )
	{
		glPointSize( 8.0f );
		glColor3f( 0.3f, 1.0f, 0.3f );
		glBegin( GL_POINTS );
		for( auto& pos : g_GameplayLog.agentTrail )
		{
			glm::vec3 p = ( pos + offset ) * scale;
			glVertex3f( p.x, p.y, p.z );
		}
		glEnd();
	}

	// Draw Known markers on entities — yellow squares for discovered waypoints
	auto knownSet = world.context().entityTagRegister().tagSet( "Known" );
	if( knownSet )
	{
		const float knownSquareSize = 0.006f;
		glPointSize( 6.0f );
		for( auto g : *knownSet )
		{
			auto e = world.entity( g );
			if( !e ) continue;
			auto loc = e->property( "Location" );
			if( !loc ) continue;
			glm::vec3 p = ( *loc->getVec3() + offset ) * scale;

			auto wpSet = world.context().entityTagRegister().tagSet( "Waypoint" );
			bool isWp = wpSet && wpSet->count( g );

			auto itemSet = world.context().entityTagRegister().tagSet( "Item" );
			bool isItem = itemSet && itemSet->count( g );

			auto portalSet = world.context().entityTagRegister().tagSet( "Portal" );
			bool isAccess = portalSet && portalSet->count( g );

			if( isWp )
			{
				// Discovered waypoints: yellow filled square
				glColor3f( 1.0f, 0.9f, 0.0f );
				glBegin( GL_QUADS );
				glVertex3f( p.x - knownSquareSize, p.y - knownSquareSize, p.z );
				glVertex3f( p.x + knownSquareSize, p.y - knownSquareSize, p.z );
				glVertex3f( p.x + knownSquareSize, p.y + knownSquareSize, p.z );
				glVertex3f( p.x - knownSquareSize, p.y + knownSquareSize, p.z );
				glEnd();
			}
			else
			{
				if( isItem )        glColor3f( 0.2f, 0.8f, 1.0f );
				else if( isAccess ) glColor3f( 1.0f, 1.0f, 0.2f );
				else                glColor3f( 1.0f, 0.5f, 0.0f );

				glBegin( GL_POINTS );
				glVertex3f( p.x, p.y, p.z );
				glEnd();
			}
		}
	}

	// Draw movement paths from cycle action paths (cycle-age color differentiation)
	{
		int highlightCycle = g_GameplayLog.selectedCycle;
		size_t totalCycles = g_CycleActionPaths.size();
		for( size_t ci = 0; ci < totalCycles; ci++ )
		{
			if( highlightCycle >= 0 && static_cast<int>( ci ) != highlightCycle ) continue;

			bool isSelected = ( highlightCycle >= 0 );
			SetCyclePathStyle( ci, totalCycles, isSelected );

			auto& cyclePaths = g_CycleActionPaths[ci];
			for( size_t ai = 0; ai < cyclePaths.size(); ai++ )
			{
				auto& path = cyclePaths[ai];
				if( path.size() < 2 ) continue;

				glBegin( GL_LINE_STRIP );
				for( auto& pos : path )
				{
					glm::vec3 p = ( pos + offset ) * scale;
					glVertex3f( p.x, p.y, p.z );
				}
				glEnd();

				// Draw waypoint dots along the path
				glPointSize( CyclePathPointSize( ci, totalCycles ) );
				SetCyclePathDotColor( ci, totalCycles, isSelected );
				glBegin( GL_POINTS );
				for( size_t pi = 1; pi < path.size() - 1; pi++ )
				{
					glm::vec3 p = ( path[pi] + offset ) * scale;
					glVertex3f( p.x, p.y, p.z );
				}
				glEnd();
			}
		}
		glLineWidth( 1.0f );
	}

	// Draw safe marker
	auto safe = FindEntityByName( world.context(), "Safe" );
	if( safe )
	{
		auto safeRoom = safe->property( "InRoom" );
		if( safeRoom )
		{
			auto roomEnt = world.entity( *safeRoom->getGuid() );
			if( roomEnt )
			{
				auto loc = roomEnt->property( "Location" );
				if( loc )
				{
					glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
					bool open = *safe->property( "Open" )->getBool();
					if( open ) glColor3f( 0.0f, 1.0f, 0.0f );
					else       glColor3f( 1.0f, 0.2f, 0.2f );
					float s = 0.025f;
					glBegin( GL_QUADS );
					glVertex3f( p.x - s, p.y - s, p.z );
					glVertex3f( p.x + s, p.y - s, p.z );
					glVertex3f( p.x + s, p.y + s, p.z );
					glVertex3f( p.x - s, p.y + s, p.z );
					glEnd();
				}
			}
		}
	}

	// Draw EnergyPanel marker
	auto panel = FindEntityByName( world.context(), "EnergyPanel" );
	if( panel )
	{
		auto panelKnown = world.context().entityTagRegister().tagSet( "Known" );
		if( panelKnown && panelKnown->count( panel->guid() ) )
		{
			auto loc = panel->property( "Location" );
			if( loc )
			{
				glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
				bool panelOpen = *panel->property( "Open" )->getBool();
				if( panelOpen ) glColor3f( 0.0f, 0.8f, 1.0f );
				else            glColor3f( 1.0f, 0.6f, 0.0f );
				float s = 0.02f;
				glBegin( GL_TRIANGLES );
				glVertex3f( p.x, p.y - s, p.z ); glVertex3f( p.x - s, p.y, p.z ); glVertex3f( p.x + s, p.y, p.z );
				glVertex3f( p.x, p.y + s, p.z ); glVertex3f( p.x - s, p.y, p.z ); glVertex3f( p.x + s, p.y, p.z );
				glEnd();
			}
		}
	}

	// Draw agent location as magenta dot
	if( planner.agent() )
	{
		auto agentEnt = world.entity( planner.agent()->guid() );
		if( agentEnt )
		{
			auto agentLoc = agentEnt->property( "Location" );
			if( agentLoc )
			{
				glm::vec3 p = ( *agentLoc->getVec3() + offset ) * scale;
				glPointSize( 14.0f );
				glColor3f( 1.0f, 0.0f, 1.0f );
				glBegin( GL_POINTS );
				glVertex3f( p.x, p.y, p.z );
				glEnd();
			}
		}
	}
}

// ---------------------------------------------------------------------------
// ImGui panel — gameplay log and world state
// ---------------------------------------------------------------------------
static void ImGuiFunc6( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimGuid )
{
	ImGui::TextUnformatted( "Heist - Multi-Plan Discovery (Example 6)" );
	ImGui::Separator();

	if( ImGui::CollapsingHeader( "Settings", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		ImGui::SliderFloat( "Travel Cost Weight", &g_Toggles.travelCostWeight, 0.1f, 3.0f );
		ImGui::Checkbox( "Show Full Map (debug)", &g_Toggles.showFullMap );
	}

	// Show agent state
	const gie::Blackboard* ctx = &world.context();
	auto agentEnt = ctx->entity( planner.agent() ? planner.agent()->guid() : gie::NullGuid );
	if( agentEnt )
	{
		auto L = *agentEnt->property( "Location" )->getVec3();
		ImGui::Text( "Agent: (%.1f, %.1f)", L.x, L.y );

		int known = CountKnown( *ctx );
		ImGui::Text( "Known entities: %d", known );

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
					auto n = gie::stringRegister().get( e->nameHash() );
					ImGui::Text( "- %.*s", (int)n.size(), n.data() );
				}
			}
			ImGui::Unindent();
		}
	}

	// Show safe and alarm status
	auto safe = FindEntityByName( *ctx, "Safe" );
	if( safe )
	{
		bool open = *const_cast<gie::Entity*>( safe )->property( "Open" )->getBool();
		ImGui::Text( "Safe: %s", open ? "OPEN" : "Locked" );
	}
	auto alarm = FindEntityByName( *ctx, "AlarmSystem" );
	if( alarm )
	{
		bool armed = *const_cast<gie::Entity*>( alarm )->property( "Armed" )->getBool();
		ImGui::Text( "Alarm: %s", armed ? "ARMED" : "Disabled" );
	}

	ImGui::Separator();

	// Step execution mode
	static bool stepExecution = false;
	bool finished = g_GameplayLog.primaryGoalReached
		|| ( !g_GameplayLog.cycles.empty() && g_GameplayLog.cycles.back().goalType == "Stuck" );

	// Before gameplay starts: checkbox + Start button
	if( !g_GameplayLog.started )
	{
		ImGui::Checkbox( "Step Execution", &stepExecution );
		ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.2f, 0.6f, 0.2f, 1.0f ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.3f, 0.8f, 0.3f, 1.0f ) );
		if( ImGui::Button( "Start Gameplay", ImVec2( -1.f, 40.f ) ) )
		{
			auto* agentPtr = planner.agent();
			if( agentPtr )
			{
				g_GameplayLog = {};
				g_CycleActionPaths.clear();
				g_LastActionPath.clear();
				g_GameplayLog.started = true;
				g_GameplayLog.agentTrail.push_back( *agentPtr->property( "Location" )->getVec3() );

				if( stepExecution )
				{
					RunGameplayCycle( world, agentPtr );
				}
				else
				{
					const int maxCycles = 30;
					for( int i = 0; i < maxCycles; i++ )
					{
						CycleResult result = RunGameplayCycle( world, agentPtr );
						if( result != CycleResult::Continued )
							break;
					}
				}
			}
		}
		ImGui::PopStyleColor( 2 );
		ImGui::TextWrapped( "Click to run the plan-execute-replan gameplay loop. "
			"The agent explores the mansion perimeter, disables security, and opens the safe." );
		return;
	}

	// Gameplay log
	if( ImGui::CollapsingHeader( "Gameplay Log", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		// Run / Step buttons (at top of log for easy access)
		if( stepExecution && !finished )
		{
			float buttonWidth = ( ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x ) * 0.5f;

			ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.2f, 0.6f, 0.2f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.3f, 0.8f, 0.3f, 1.0f ) );
			if( ImGui::Button( "Run", ImVec2( buttonWidth, 40.f ) ) )
			{
				auto* agentPtr = planner.agent();
				if( agentPtr )
				{
					const int maxCycles = 30 - static_cast<int>( g_GameplayLog.cycles.size() );
					for( int i = 0; i < maxCycles; i++ )
					{
						CycleResult result = RunGameplayCycle( world, agentPtr );
						if( result != CycleResult::Continued )
							break;
					}
				}
			}
			ImGui::PopStyleColor( 2 );

			ImGui::SameLine();

			ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.2f, 0.4f, 0.7f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.3f, 0.5f, 0.9f, 1.0f ) );
			if( ImGui::Button( "Step", ImVec2( buttonWidth, 40.f ) ) )
			{
				auto* agentPtr = planner.agent();
				if( agentPtr )
					RunGameplayCycle( world, agentPtr );
			}
			ImGui::PopStyleColor( 2 );
		}

		if( g_GameplayLog.primaryGoalReached )
			ImGui::TextColored( ImVec4( 0.4f, 1.0f, 0.4f, 1.0f ), "Goal reached: YES" );
		else
			ImGui::Text( "Goal reached: No" );
		ImGui::Text( "Total cycles: %zu", g_GameplayLog.cycles.size() );

		auto knownSet = ctx->entityTagRegister().tagSet( "Known" );
		if( ImGui::CollapsingHeader( "Known Entities" ) )
		{
			if( knownSet )
			{
				for( auto g : *knownSet )
				{
					auto e = ctx->entity( g );
					if( e )
					{
						auto nm = gie::stringRegister().get( e->nameHash() );
						auto loc = e->property( "Location" );
						if( loc )
							ImGui::Text( "  %.*s (%.0f, %.0f)", (int)nm.size(), nm.data(), loc->getVec3()->x, loc->getVec3()->y );
						else
							ImGui::Text( "  %.*s", (int)nm.size(), nm.data() );
					}
				}
			}
		}

		ImGui::Separator();

		for( size_t ri = 0; ri < g_GameplayLog.cycles.size(); ri++ )
		{
			size_t i = g_GameplayLog.cycles.size() - 1 - ri;
			auto& c = g_GameplayLog.cycles[i];

			ImVec4 color = ( c.goalType == "Primary" ) ? ImVec4( 0.4f, 1.0f, 0.4f, 1.0f )
				: ( c.goalType == "Explore" ) ? ImVec4( 0.4f, 0.6f, 1.0f, 1.0f )
				: ImVec4( 1.0f, 0.4f, 0.4f, 1.0f );
			ImGui::PushStyleColor( ImGuiCol_Text, color );

			char label[128];
			std::snprintf( label, sizeof( label ), "Cycle %d [%s] %zu sims, %zu actions###cycle%zu",
				c.cycle, c.goalType.c_str(), c.simulationCount, c.actionNames.size(), i );

			bool nodeOpen = ImGui::TreeNodeEx( label, ImGuiTreeNodeFlags_DefaultOpen );
			ImGui::PopStyleColor();

			// Click cycle header to highlight its paths in the GL view
			if( ImGui::IsItemClicked() )
				g_GameplayLog.selectedCycle = ( g_GameplayLog.selectedCycle == static_cast<int>( i ) ) ? -1 : static_cast<int>( i );

			if( nodeOpen )
			{
				ImGui::TextUnformatted( "Executed Actions:" );
				ImGui::Indent();
				for( size_t ai = 0; ai < c.actionNames.size(); ai++ )
				{
					bool isLast = ( i == g_GameplayLog.cycles.size() - 1 && ai == c.actionNames.size() - 1 );
					if( isLast && g_GameplayLog.primaryGoalReached )
						ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 0.85f, 0.0f, 1.0f ) );

					std::string detail = ( ai < c.actionDetails.size() && !c.actionDetails[ai].empty() )
						? c.actionNames[ai] + " — " + c.actionDetails[ai]
						: c.actionNames[ai];
					ImGui::Text( "%zu. %s", ai + 1, detail.c_str() );

					// Show path waypoints for movement actions
					if( i < g_CycleActionPaths.size() && ai < g_CycleActionPaths[i].size()
						&& g_CycleActionPaths[i][ai].size() > 2 )
					{
						ImGui::SameLine();
						ImGui::TextColored( ImVec4( 1.0f, 0.84f, 0.0f, 0.7f ),
							"(%zu waypoints)", g_CycleActionPaths[i][ai].size() - 1 );
					}

					if( isLast && g_GameplayLog.primaryGoalReached )
						ImGui::PopStyleColor();
				}
				ImGui::Unindent();

				ImGui::Text( "Pos: (%.0f,%.0f) -> (%.0f,%.0f)",
					c.agentPosBefore.x, c.agentPosBefore.y,
					c.agentPosAfter.x, c.agentPosAfter.y );
				ImGui::Text( "Known: %d -> %d", c.knownCountBefore, c.knownCountAfter );
				if( !c.inventoryAfter.empty() )
				{
					ImGui::Text( "Inventory:" );
					ImGui::Indent();
					for( auto& item : c.inventoryAfter )
						ImGui::Text( "%s", item.c_str() );
					ImGui::Unindent();
				}

				if( c.plannerPtr )
				{
					auto* rootSim = c.plannerPtr->rootSimulation();
					if( rootSim )
					{
						char treeLabel[64];
						std::snprintf( treeLabel, sizeof( treeLabel ), "Simulation Tree (%zu nodes)###simtree%zu",
							c.simulationCount, i );
						if( ImGui::TreeNode( treeLabel ) )
						{
							drawSimulationTreeView( *c.plannerPtr, rootSim );
							ImGui::TreePop();
						}
					}
				}

				ImGui::TreePop();
			}
		}
	}

	// Resolve selectedSimulationGuid across all cycle planners for blackboard view
	{
		const gie::Simulation* selectedSim = nullptr;
		for( auto& c : g_GameplayLog.cycles )
		{
			if( !c.plannerPtr ) continue;
			selectedSim = c.plannerPtr->simulation( selectedSimGuid );
			if( selectedSim ) break;
		}
		if( !selectedSim )
			selectedSim = planner.simulation( selectedSimGuid );
		if( selectedSim )
			drawBlackboardPropertiesWindow( selectedSim );
	}
}

// ---------------------------------------------------------------------------
// FindRoom helper
// ---------------------------------------------------------------------------
static gie::Entity* FindRoom( gie::World& world, const char* roomName )
{
	for( const auto& kv : world.context().entities() )
	{
		if( gie::stringRegister().get( kv.second.nameHash() ) == roomName ) return world.entity( kv.first );
	}
	return nullptr;
}
