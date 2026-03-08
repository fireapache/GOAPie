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

// Discovery Example (Example 7)
// Detective treasure hunt on an island with partial observability.
// This example demonstrates a realistic gameplay loop where the planner is
// called multiple times. The gameplay character:
//   1. Tries the primary goal (open treasure chest)
//   2. If unreachable, falls back to an exploration goal
//   3. Executes planned actions, which modify the real world
//   4. Replans — cycling between primary and exploration until success
//
// Key concepts:
//   - Partial world knowledge: agent discovers entities at runtime
//   - Primary/secondary goals with automatic fallback
//   - Multiple planning cycles with world mutation between plans
//   - False clues leading to dead ends
//   - Tool crafting gating (forge key from ore)

const char* treasureHuntDescription()
{
	return "Discovery scenario: explore an island, follow clues, and find a hidden treasure chest.";
}

// Helpers
static inline float clampf( float v, float lo, float hi ) { return std::max( lo, std::min( v, hi ) ); }
using gie::Guid;
static inline gie::StringHash H( const char* s ) { return gie::stringHasher( s ); }

// Find entity by name walking parent chain
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

// UI toggles
struct DiscoveryToggles
{
	float travelCostWeight{ 1.0f };
	bool showFullMap{ false };
};
static DiscoveryToggles g_Toggles{};

// Forward declarations
static void ImGuiFunc7( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimulationGuid );
static void GLDrawFunc7( gie::World& world, gie::Planner& planner );

// ---------------------------------------------------------------------------
// Gameplay Log — records each planning cycle for visualization
// ---------------------------------------------------------------------------
struct GameplayCycleEntry
{
	int cycle{ 0 };
	std::string goalType;          // "Primary" or "Explore"
	bool planFound{ false };
	std::vector<std::string> actionNames;   // execution order (first to last)
	std::vector<std::string> actionDetails; // detailed description per action
	glm::vec3 agentPosBefore{ 0.f };
	glm::vec3 agentPosAfter{ 0.f };
	int knownCountBefore{ 0 };
	int knownCountAfter{ 0 };
	std::vector<std::string> inventoryAfter;
	size_t simulationCount{ 0 };
	std::unique_ptr<gie::Planner> plannerPtr; // preserved planner with full simulation tree
};

struct GameplayLog
{
	std::vector<GameplayCycleEntry> cycles;
	std::vector<glm::vec3> agentTrail; // breadcrumb trail of positions
	bool primaryGoalReached{ false };
	int selectedCycle{ -1 }; // for UI highlight
	bool started{ false };   // has the gameplay loop been executed?
};

static GameplayLog g_GameplayLog;

// ---------------------------------------------------------------------------
// World layout — an island with 8 areas:
//
//              Village (60, 40)
//               /
//         Cliff (40, 25)
//          /          \
//   Waterfall (55, 10) Ruins (20, 30)
//       |                /        \
//   HiddenCave (65, 0) Jungle (0, 15)  Swamp (35, 5)
//                        |
//                     Beach (0, 0)  <-- agent starts here
// ---------------------------------------------------------------------------

// Cost helper
static void SetSimulationCost( gie::Simulation& sim, float travelLength, float baseActionCost )
{
	sim.cost = travelLength * g_Toggles.travelCostWeight + baseActionCost;
}

// Move agent along waypoint path
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

// Get all waypoints for pathfinding in simulation
static std::vector<Guid> getAllWaypoints( const gie::Simulation& sim )
{
	std::vector<Guid> result;
	if( auto set = sim.tagSet( "Waypoint" ) )
		result.assign( set->begin(), set->end() );
	return result;
}

// Check if agent has item with given info name in inventory
static bool HasItemWithInfo( const gie::Blackboard& ctx, Guid agentGuid, const char* infoName )
{
	auto agent = ctx.entity( agentGuid );
	if( !agent ) return false;
	auto inv = agent->property( "Inventory" );
	if( !inv ) return false;
	auto arr = inv->getGuidArray();
	if( !arr ) return false;
	for( Guid g : *arr )
	{
		auto e = ctx.entity( g );
		if( !e ) continue;
		auto info = e->property( "Info" );
		if( !info ) continue;
		auto infoEnt = ctx.entity( *info->getGuid() );
		if( infoEnt && gie::stringRegister().get( infoEnt->nameHash() ) == std::string_view( infoName ) )
			return true;
	}
	return false;
}

// Count known waypoints
static int CountKnown( const gie::Blackboard& ctx )
{
	auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
	if( !knownSet ) return 0;
	auto wpSet = ctx.entityTagRegister().tagSet( "Waypoint" );
	if( !wpSet ) return 0;
	int count = 0;
	for( auto g : *knownSet )
		if( wpSet->count( g ) ) count++;
	return count;
}

// Heuristic for treasure goal
static float EstimateTreasureHeuristic( const gie::Simulation& sim, const gie::Entity* agentEnt )
{
	if( !agentEnt ) return 0.f;

	auto chest = FindEntityByName( sim.context(), "LockedChest" );
	if( chest )
	{
		auto openProp = chest->property( "Open" );
		if( openProp && *openProp->getBool() ) return 0.f;
	}

	float h = 0.f;
	if( !chest ) h += 30.f;
	if( !HasItemWithInfo( sim.context(), agentEnt->guid(), "TreasureKeyInfo" ) )
		h += 20.f;
	if( !HasItemWithInfo( sim.context(), agentEnt->guid(), "IronOreInfo" ) )
		h += 10.f;
	if( chest )
	{
		auto agentLoc = agentEnt->property( "Location" );
		auto chestLoc = chest->property( "Location" );
		if( agentLoc && chestLoc )
			h += glm::distance( *agentLoc->getVec3(), *chestLoc->getVec3() ) * 0.1f;
	}
	return h;
}

// ---------------------------------------------------------------------------
// World setup
// ---------------------------------------------------------------------------
static gie::Agent* treasureHunt_world( ExampleParameters& params )
{
	gie::World& world = params.world;
	params.imGuiDrawFunc = &ImGuiFunc7;
	params.glDrawFunc = &GLDrawFunc7;
	params.isGameplayExample = true;

	auto selectableTag = H( "Selectable" );
	auto drawTag = H( "Draw" );

	// Agent
	auto agent = world.createAgent( "Explorer" );
	world.context().entityTagRegister().tag( agent, { H( "Agent" ) } );
	agent->createProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
	agent->createProperty( "Inventory", gie::Property::GuidVector{} );
	agent->createProperty( "DiscoveryCount", 1.f );
	agent->createProperty( "ExploredNewArea", false ); // exploration goal target

	// Archetypes
	if( auto* a = world.createArchetype( "Waypoint" ) )
	{
		a->addTag( selectableTag );
		a->addTag( "Waypoint" );
		a->addTag( drawTag );
		a->addProperty( "Location", glm::vec3{ 0.f } );
		a->addProperty( "Links", gie::Property::GuidVector{} );
		a->addProperty( "Reveals", gie::Property::GuidVector{} );
	}
	if( auto* a = world.createArchetype( "Item" ) )
	{
		a->addTag( selectableTag );
		a->addTag( "Item" );
		a->addProperty( "Location", glm::vec3{ 0.f } );
		a->addProperty( "Info", gie::NullGuid );
	}
	if( auto* a = world.createArchetype( "Info" ) )
	{
		a->addTag( "Info" );
		a->addProperty( "Name", gie::NullGuid );
	}
	if( auto* a = world.createArchetype( "Clue" ) )
	{
		a->addTag( selectableTag );
		a->addTag( "Clue" );
		a->addTag( drawTag );
		a->addProperty( "Location", glm::vec3{ 0.f } );
		a->addProperty( "Inspected", false );
		a->addProperty( "RevealTarget", gie::NullGuid );
		a->addProperty( "IsFalseClue", false );
	}
	if( auto* a = world.createArchetype( "Chest" ) )
	{
		a->addTag( selectableTag );
		a->addTag( drawTag );
		a->addProperty( "Open", false );
		a->addProperty( "Location", glm::vec3{ 0.f } );
		a->addProperty( "RequiredKey", gie::NullGuid );
	}
	if( auto* a = world.createArchetype( "Forge" ) )
	{
		a->addTag( selectableTag );
		a->addTag( drawTag );
		a->addTag( "Forge" );
		a->addProperty( "Location", glm::vec3{ 0.f } );
	}

	// -- Create waypoints --
	struct WPDef { const char* name; glm::vec3 pos; };
	const WPDef wpDefs[] = {
		{ "WP_Beach",       {  0.f,   0.f, 0.f } },
		{ "WP_Jungle",      {  0.f,  15.f, 0.f } },
		{ "WP_Ruins",       { 20.f,  30.f, 0.f } },
		{ "WP_Swamp",       { 35.f,   5.f, 0.f } },
		{ "WP_Cliff",       { 40.f,  25.f, 0.f } },
		{ "WP_Waterfall",   { 55.f,  10.f, 0.f } },
		{ "WP_HiddenCave",  { 65.f,   0.f, 0.f } },
		{ "WP_Village",     { 60.f,  40.f, 0.f } },
	};
	constexpr size_t wpCount = std::size( wpDefs );

	std::vector<gie::Entity*> wps;
	std::vector<gie::Property::GuidVector*> wpLinks;
	std::vector<gie::Property::GuidVector*> wpReveals;
	wps.reserve( wpCount );
	wpLinks.reserve( wpCount );
	wpReveals.reserve( wpCount );

	for( size_t i = 0; i < wpCount; i++ )
	{
		auto e = world.createEntity( wpDefs[i].name );
		world.context().entityTagRegister().tag( e, { H( "Waypoint" ), drawTag, selectableTag } );
		e->createProperty( "Location", wpDefs[i].pos );
		auto lp = e->createProperty( "Links", gie::Property::GuidVector{} );
		auto rp = e->createProperty( "Reveals", gie::Property::GuidVector{} );
		wps.push_back( e );
		wpLinks.push_back( lp->getGuidArray() );
		wpReveals.push_back( rp->getGuidArray() );
	}

	auto link = [&]( size_t a, size_t b )
	{
		wpLinks[a]->push_back( wps[b]->guid() );
		wpLinks[b]->push_back( wps[a]->guid() );
	};

	link( 0, 1 ); // Beach <-> Jungle
	link( 1, 2 ); // Jungle <-> Ruins
	link( 1, 3 ); // Jungle <-> Swamp
	link( 2, 4 ); // Ruins <-> Cliff
	link( 4, 5 ); // Cliff <-> Waterfall
	link( 5, 6 ); // Waterfall <-> HiddenCave
	link( 4, 7 ); // Cliff <-> Village

	world.context().entityTagRegister().tag( wps[0], { H( "Known" ) } );

	// -- Info entities --
	auto makeInfo = [&]( const char* infoName ) -> gie::Entity*
	{
		auto e = world.createEntity( infoName );
		e->createProperty( "Name", infoName );
		world.context().entityTagRegister().tag( e, { H( "Info" ) } );
		return e;
	};

	auto infoOldMap      = makeInfo( "OldMapInfo" );
	auto infoMachete     = makeInfo( "MacheteInfo" );
	auto infoIronOre     = makeInfo( "IronOreInfo" );
	auto infoMuddyNote   = makeInfo( "MuddyNoteInfo" );
	auto infoTreasureKey  = makeInfo( "TreasureKeyInfo" );

	// -- Items --
	auto placeItem = [&]( const char* itemName, gie::Entity* info, glm::vec3 pos ) -> gie::Entity*
	{
		auto e = world.createEntity( itemName );
		world.context().entityTagRegister().tag( e, { H( "Item" ) } );
		e->createProperty( "Location", pos );
		e->createProperty( "Info", info ? info->guid() : gie::NullGuid );
		return e;
	};

	auto oldMap = placeItem( "OldMap", infoOldMap, wpDefs[1].pos );
	placeItem( "Machete", infoMachete, wpDefs[1].pos + glm::vec3{ 3.f, 0.f, 0.f } );
	auto ironOre = placeItem( "IronOre", infoIronOre, wpDefs[2].pos );
	auto muddyNote = placeItem( "MuddyNote", infoMuddyNote, wpDefs[3].pos );

	// -- Clues --
	auto clueOldMap = world.createEntity( "ClueOldMap" );
	world.context().entityTagRegister().tag( clueOldMap, { H( "Clue" ), drawTag, selectableTag } );
	clueOldMap->createProperty( "Location", wpDefs[1].pos );
	clueOldMap->createProperty( "Inspected", false );
	clueOldMap->createProperty( "RevealTarget", wps[3]->guid() );
	clueOldMap->createProperty( "IsFalseClue", true );

	auto clueInscription = world.createEntity( "ClueInscription" );
	world.context().entityTagRegister().tag( clueInscription, { H( "Clue" ), drawTag, selectableTag } );
	clueInscription->createProperty( "Location", wpDefs[2].pos + glm::vec3{ 2.f, 2.f, 0.f } );
	clueInscription->createProperty( "Inspected", false );
	clueInscription->createProperty( "RevealTarget", wps[5]->guid() );
	clueInscription->createProperty( "IsFalseClue", false );

	auto clueMuddyNote = world.createEntity( "ClueMuddyNote" );
	world.context().entityTagRegister().tag( clueMuddyNote, { H( "Clue" ), drawTag, selectableTag } );
	clueMuddyNote->createProperty( "Location", wpDefs[3].pos );
	clueMuddyNote->createProperty( "Inspected", false );
	clueMuddyNote->createProperty( "RevealTarget", gie::NullGuid );
	clueMuddyNote->createProperty( "IsFalseClue", true );

	// -- Observe reveals --
	wpReveals[0]->push_back( wps[1]->guid() );
	wpReveals[1]->push_back( wps[2]->guid() );
	wpReveals[1]->push_back( clueOldMap->guid() );
	wpReveals[1]->push_back( oldMap->guid() );
	wpReveals[2]->push_back( wps[4]->guid() );
	wpReveals[2]->push_back( clueInscription->guid() );
	wpReveals[2]->push_back( ironOre->guid() );
	wpReveals[3]->push_back( clueMuddyNote->guid() );
	wpReveals[3]->push_back( muddyNote->guid() );
	wpReveals[4]->push_back( wps[5]->guid() );
	wpReveals[4]->push_back( wps[7]->guid() );
	wpReveals[5]->push_back( wps[6]->guid() );

	// -- Locked Chest in HiddenCave --
	auto chest = world.createEntity( "LockedChest" );
	world.context().entityTagRegister().tag( chest, { drawTag, selectableTag } );
	chest->createProperty( "Open", false );
	chest->createProperty( "Location", wpDefs[6].pos );
	chest->createProperty( "RequiredKey", infoTreasureKey->guid() );
	wpReveals[6]->push_back( chest->guid() );

	// -- Blacksmith Forge in Village --
	auto forge = world.createEntity( "Blacksmith" );
	world.context().entityTagRegister().tag( forge, { H( "Forge" ), drawTag, selectableTag } );
	forge->createProperty( "Location", wpDefs[7].pos );
	wpReveals[7]->push_back( forge->guid() );

	return agent;
}

// ---------------------------------------------------------------------------
// Action Simulators — used by planner during planning
// ---------------------------------------------------------------------------

static void RegisterActions( gie::Planner& planner )
{
	// Shared heuristic for treasure goal
	auto treasureHeuristic = []( gie::CalculateHeuristicParams params )
	{
		auto agentEnt = params.simulation.context().entity( params.agent.guid() );
		params.simulation.heuristic.value = EstimateTreasureHeuristic( params.simulation, agentEnt );
	};

	// -----------------------------------------------------------------------
	// Observe: stand at a Known waypoint and reveal what it shows.
	// Opaque during planning (no reveals), sets ExploredNewArea = true.
	// Forced leaf: world changes only materialize during gameplay execution.
	// -----------------------------------------------------------------------
	planner.addLambdaAction( "Observe",
		// evaluate
		[]( gie::EvaluateSimulationParams params ) -> bool
		{
			params.addDebugMessage( "Observe::evaluate" );
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			if( !agentEnt ) return false;
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			auto wpSet = params.simulation.tagSet( "Waypoint" );
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			if( !wpSet || !knownSet ) { params.addDebugMessage( "  No waypoints or known set -> FALSE" ); return false; }

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
					if( reveals )
					{
						auto revArr = reveals->getGuidArray();
						if( revArr )
						{
							for( Guid rg : *revArr )
							{
								if( !knownSet->count( rg ) )
								{
									params.addDebugMessage( "  Unrevealed entities at this waypoint -> TRUE" );
									return true;
								}
							}
						}
					}
				}
			}
			params.addDebugMessage( "  Nothing new to observe -> FALSE" );
			return false;
		},
		// simulate
		[]( gie::SimulateSimulationParams params ) -> bool
		{
			params.addDebugMessage( "Observe::simulate (opaque — no reveals during planning)" );
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			auto explored = agentEnt->property( "ExploredNewArea" );
			if( explored ) explored->value = true;

			SetSimulationCost( params.simulation, 0.f, 2.f );
			return true;
		},
		treasureHeuristic,
		true // forceLeaf
	);

	// -----------------------------------------------------------------------
	// MoveTo: move to a Known waypoint (prefer unexplored neighbors)
	// -----------------------------------------------------------------------
	planner.addLambdaAction( "MoveTo",
		// evaluate
		[]( gie::EvaluateSimulationParams params ) -> bool
		{
			params.addDebugMessage( "MoveTo::evaluate" );
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			if( !agentEnt ) return false;
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			auto wpSet = params.simulation.tagSet( "Waypoint" );
			if( !knownSet || !wpSet ) return false;

			for( auto g : *wpSet )
			{
				if( !knownSet->count( g ) ) continue;
				auto wp = ctx.entity( g );
				if( !wp ) continue;
				auto loc = wp->property( "Location" );
				if( !loc ) continue;
				if( glm::distance( agentLoc, *loc->getVec3() ) > 1.f )
				{
					params.addDebugMessage( "  Reachable known waypoint -> TRUE" );
					return true;
				}
			}
			params.addDebugMessage( "  Already at all known waypoints -> FALSE" );
			return false;
		},
		// simulate
		[]( gie::SimulateSimulationParams params ) -> bool
		{
			params.addDebugMessage( "MoveTo::simulate" );
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			auto wpSet = params.simulation.tagSet( "Waypoint" );
			if( !knownSet || !wpSet ) return false;

			gie::Entity* bestTarget = nullptr;
			float bestScore = std::numeric_limits<float>::max();

			for( auto g : *wpSet )
			{
				if( !knownSet->count( g ) ) continue;
				auto wp = ctx.entity( g );
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

			if( !bestTarget ) return false;

			auto allWp = getAllWaypoints( params.simulation );
			float len = MoveAgentAlongPath( params, agentLoc, bestTarget, allWp );

			SetSimulationCost( params.simulation, len, 1.f );
			return true;
		},
		treasureHeuristic
	);

	// -----------------------------------------------------------------------
	// Inspect: inspect a Known, un-inspected Clue.
	// Opaque during planning (no reveals). Forced leaf.
	// -----------------------------------------------------------------------
	planner.addLambdaAction( "Inspect",
		// evaluate
		[]( gie::EvaluateSimulationParams params ) -> bool
		{
			params.addDebugMessage( "Inspect::evaluate" );
			auto& ctx = params.simulation.context();
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			auto clueSet = params.simulation.tagSet( "Clue" );
			if( !knownSet || !clueSet ) { params.addDebugMessage( "  No clues -> FALSE" ); return false; }

			for( auto g : *clueSet )
			{
				if( !knownSet->count( g ) ) continue;
				auto clue = ctx.entity( g );
				if( !clue ) continue;
				auto inspected = clue->property( "Inspected" );
				if( inspected && !*inspected->getBool() )
				{
					params.addDebugMessage( "  Known un-inspected clue -> TRUE" );
					return true;
				}
			}
			params.addDebugMessage( "  No un-inspected clues -> FALSE" );
			return false;
		},
		// simulate
		[]( gie::SimulateSimulationParams params ) -> bool
		{
			params.addDebugMessage( "Inspect::simulate (opaque — no reveals during planning)" );
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			auto clueSet = params.simulation.tagSet( "Clue" );
			if( !knownSet || !clueSet ) return false;

			gie::Entity* bestClue = nullptr;
			float bestDist = std::numeric_limits<float>::max();

			for( auto g : *clueSet )
			{
				if( !knownSet->count( g ) ) continue;
				auto clue = ctx.entity( g );
				if( !clue ) continue;
				auto inspected = clue->property( "Inspected" );
				if( !inspected || *inspected->getBool() ) continue;
				auto loc = clue->property( "Location" );
				if( !loc ) continue;
				float dist = glm::distance( agentLoc, *loc->getVec3() );
				if( dist < bestDist ) { bestDist = dist; bestClue = clue; }
			}

			if( !bestClue ) return false;

			auto allWp = getAllWaypoints( params.simulation );
			float len = MoveAgentAlongPath( params, agentLoc, bestClue, allWp );
			bestClue->property( "Inspected" )->value = true;
			// Opaque: do NOT reveal targets during planning
			auto explored = agentEnt->property( "ExploredNewArea" );
			if( explored ) explored->value = true;

			SetSimulationCost( params.simulation, len, 3.f );
			return true;
		},
		treasureHeuristic,
		true // forceLeaf
	);

	// -----------------------------------------------------------------------
	// PickUp: collect a Known Item not already in inventory
	// -----------------------------------------------------------------------
	planner.addLambdaAction( "PickUp",
		// evaluate
		[]( gie::EvaluateSimulationParams params ) -> bool
		{
			params.addDebugMessage( "PickUp::evaluate" );
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			auto inv = agentEnt->property( "Inventory" )->getGuidArray();
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			auto itemSet = params.simulation.tagSet( "Item" );
			if( !knownSet || !itemSet ) { params.addDebugMessage( "  No items -> FALSE" ); return false; }

			for( auto g : *itemSet )
			{
				if( !knownSet->count( g ) ) continue;
				if( std::find( inv->begin(), inv->end(), g ) != inv->end() ) continue;
				params.addDebugMessage( "  Known item not in inventory -> TRUE" );
				return true;
			}
			params.addDebugMessage( "  No pickable items -> FALSE" );
			return false;
		},
		// simulate
		[]( gie::SimulateSimulationParams params ) -> bool
		{
			params.addDebugMessage( "PickUp::simulate" );
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			auto inv = agentEnt->property( "Inventory" )->getGuidArray();
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			auto itemSet = params.simulation.tagSet( "Item" );
			if( !knownSet || !itemSet ) return false;

			auto allWp = getAllWaypoints( params.simulation );
			gie::Entity* best = nullptr;
			float bestDist = std::numeric_limits<float>::max();

			for( auto g : *itemSet )
			{
				if( !knownSet->count( g ) ) continue;
				if( std::find( inv->begin(), inv->end(), g ) != inv->end() ) continue;
				auto e = ctx.entity( g );
				if( !e ) continue;
				auto loc = e->property( "Location" );
				if( !loc ) continue;
				float dist = glm::distance( agentLoc, *loc->getVec3() );
				if( dist < bestDist ) { bestDist = dist; best = e; }
			}

			if( !best ) return false;

			float len = MoveAgentAlongPath( params, agentLoc, best, allWp );
			inv->push_back( best->guid() );

			SetSimulationCost( params.simulation, len, 1.f );
			return true;
		},
		treasureHeuristic
	);

	// -----------------------------------------------------------------------
	// ForgeKey: at the Blacksmith, convert IronOre into TreasureKey
	// -----------------------------------------------------------------------
	planner.addLambdaAction( "ForgeKey",
		// evaluate
		[]( gie::EvaluateSimulationParams params ) -> bool
		{
			params.addDebugMessage( "ForgeKey::evaluate" );
			auto& ctx = params.simulation.context();

			auto forge = FindEntityByName( ctx, "Blacksmith" );
			if( !forge ) { params.addDebugMessage( "  Blacksmith not known -> FALSE" ); return false; }
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			if( !knownSet || !knownSet->count( forge->guid() ) ) { params.addDebugMessage( "  Blacksmith not known -> FALSE" ); return false; }

			if( !HasItemWithInfo( ctx, params.agent.guid(), "IronOreInfo" ) )
			{
				params.addDebugMessage( "  No IronOre -> FALSE" );
				return false;
			}
			if( HasItemWithInfo( ctx, params.agent.guid(), "TreasureKeyInfo" ) )
			{
				params.addDebugMessage( "  Already has key -> FALSE" );
				return false;
			}
			params.addDebugMessage( "  Can forge key -> TRUE" );
			return true;
		},
		// simulate
		[]( gie::SimulateSimulationParams params ) -> bool
		{
			params.addDebugMessage( "ForgeKey::simulate" );
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			auto forge = FindEntityByName( ctx, "Blacksmith" );
			if( !forge ) return false;

			auto allWp = getAllWaypoints( params.simulation );
			float len = MoveAgentAlongPath( params, agentLoc, forge, allWp );

			auto keyInfo = FindEntityByName( ctx, "TreasureKeyInfo" );
			if( !keyInfo ) return false;

			auto keyItem = ctx.createEntity( "TreasureKey" );
			ctx.entityTagRegister().tag( keyItem, { H( "Item" ), H( "Known" ) } );
			keyItem->createProperty( "Location", *forge->property( "Location" )->getVec3() );
			keyItem->createProperty( "Info", keyInfo->guid() );
			agentEnt->property( "Inventory" )->getGuidArray()->push_back( keyItem->guid() );

			SetSimulationCost( params.simulation, len, 5.f );
			return true;
		},
		treasureHeuristic
	);

	// -----------------------------------------------------------------------
	// OpenChest: open the LockedChest if agent has TreasureKey
	// -----------------------------------------------------------------------
	planner.addLambdaAction( "OpenChest",
		// evaluate
		[]( gie::EvaluateSimulationParams params ) -> bool
		{
			params.addDebugMessage( "OpenChest::evaluate" );
			auto& ctx = params.simulation.context();

			auto chest = FindEntityByName( ctx, "LockedChest" );
			if( !chest ) { params.addDebugMessage( "  Chest not known -> FALSE" ); return false; }
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			if( !knownSet || !knownSet->count( chest->guid() ) ) { params.addDebugMessage( "  Chest not known -> FALSE" ); return false; }

			auto openProp = chest->property( "Open" );
			if( openProp && *openProp->getBool() ) { params.addDebugMessage( "  Already open -> FALSE" ); return false; }

			if( !HasItemWithInfo( ctx, params.agent.guid(), "TreasureKeyInfo" ) )
			{
				params.addDebugMessage( "  No key -> FALSE" );
				return false;
			}
			params.addDebugMessage( "  Can open chest -> TRUE" );
			return true;
		},
		// simulate
		[]( gie::SimulateSimulationParams params ) -> bool
		{
			params.addDebugMessage( "OpenChest::simulate" );
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			auto chest = FindEntityByName( ctx, "LockedChest" );
			if( !chest ) return false;

			auto allWp = getAllWaypoints( params.simulation );
			float len = MoveAgentAlongPath( params, agentLoc, chest, allWp );

			chest->property( "Open" )->value = true;

			SetSimulationCost( params.simulation, len, 2.f );
			return true;
		},
		treasureHeuristic
	);
}

// ---------------------------------------------------------------------------
// Execute functions — apply actions to the real world (not simulation)
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
	auto knownSet = world.context().entityTagRegister().tagSet( "Known" );
	auto wpSet = world.context().entityTagRegister().tagSet( "Waypoint" );
	if( !knownSet || !wpSet ) return "";
	gie::Entity* bestTarget = nullptr;
	float bestScore = std::numeric_limits<float>::max();
	for( auto g : *wpSet )
	{
		if( !knownSet->count( g ) ) continue;
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
			{
				for( Guid rg : *revArr )
					if( !knownSet->count( rg ) ) { score -= 50.f; break; }
			}
		}
		if( score < bestScore ) { bestScore = score; bestTarget = wp; }
	}
	if( bestTarget )
	{
		*agent->property( "Location" )->getVec3() = *bestTarget->property( "Location" )->getVec3();
		return "-> " + std::string( gie::stringRegister().get( bestTarget->nameHash() ) );
	}
	return "";
}

static std::string ExecuteInspect( gie::World& world, gie::Agent* agent )
{
	glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();
	auto knownSet = world.context().entityTagRegister().tagSet( "Known" );
	auto clueSet = world.context().entityTagRegister().tagSet( "Clue" );
	if( !knownSet || !clueSet ) return "";
	gie::Entity* bestClue = nullptr;
	float bestDist = std::numeric_limits<float>::max();
	for( auto g : *clueSet )
	{
		if( !knownSet->count( g ) ) continue;
		auto clue = world.entity( g );
		if( !clue ) continue;
		auto inspected = clue->property( "Inspected" );
		if( !inspected || *inspected->getBool() ) continue;
		auto loc = clue->property( "Location" );
		if( !loc ) continue;
		float dist = glm::distance( agentLoc, *loc->getVec3() );
		if( dist < bestDist ) { bestDist = dist; bestClue = clue; }
	}
	if( !bestClue ) return "";
	std::string clueName{ gie::stringRegister().get( bestClue->nameHash() ) };
	*agent->property( "Location" )->getVec3() = *bestClue->property( "Location" )->getVec3();
	bestClue->property( "Inspected" )->value = true;
	std::string detail = clueName;
	auto isFalse = bestClue->property( "IsFalseClue" );
	if( isFalse && *isFalse->getBool() )
		detail += " (false clue!)";
	auto revealTarget = bestClue->property( "RevealTarget" );
	if( revealTarget && *revealTarget->getGuid() != gie::NullGuid )
	{
		auto targetEnt = world.entity( *revealTarget->getGuid() );
		if( targetEnt )
		{
			world.context().entityTagRegister().tag( targetEnt, { H( "Known" ) } );
			detail += " -> revealed " + std::string( gie::stringRegister().get( targetEnt->nameHash() ) );
		}
	}
	agent->property( "ExploredNewArea" )->value = true;
	return detail;
}

static std::string ExecutePickUp( gie::World& world, gie::Agent* agent )
{
	glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();
	auto inv = agent->property( "Inventory" )->getGuidArray();
	auto knownSet = world.context().entityTagRegister().tagSet( "Known" );
	auto itemSet = world.context().entityTagRegister().tagSet( "Item" );
	if( !knownSet || !itemSet ) return "";
	gie::Entity* best = nullptr;
	float bestDist = std::numeric_limits<float>::max();
	for( auto g : *itemSet )
	{
		if( !knownSet->count( g ) ) continue;
		if( std::find( inv->begin(), inv->end(), g ) != inv->end() ) continue;
		auto e = world.entity( g );
		if( !e ) continue;
		auto loc = e->property( "Location" );
		if( !loc ) continue;
		float dist = glm::distance( agentLoc, *loc->getVec3() );
		if( dist < bestDist ) { bestDist = dist; best = e; }
	}
	if( !best ) return "";
	*agent->property( "Location" )->getVec3() = *best->property( "Location" )->getVec3();
	inv->push_back( best->guid() );
	return std::string( gie::stringRegister().get( best->nameHash() ) );
}

static std::string ExecuteForgeKey( gie::World& world, gie::Agent* agent )
{
	auto forge = FindEntityByName( world.context(), "Blacksmith" );
	if( !forge ) return "";
	*agent->property( "Location" )->getVec3() = *forge->property( "Location" )->getVec3();
	auto keyInfo = FindEntityByName( world.context(), "TreasureKeyInfo" );
	if( !keyInfo ) return "";
	auto keyItem = world.createEntity( "TreasureKey" );
	world.context().entityTagRegister().tag( keyItem, { H( "Item" ), H( "Known" ) } );
	keyItem->createProperty( "Location", *forge->property( "Location" )->getVec3() );
	keyItem->createProperty( "Info", keyInfo->guid() );
	agent->property( "Inventory" )->getGuidArray()->push_back( keyItem->guid() );
	return "IronOre -> TreasureKey at Blacksmith";
}

static std::string ExecuteOpenChest( gie::World& world, gie::Agent* agent )
{
	auto chest = FindEntityByName( world.context(), "LockedChest" );
	if( !chest ) return "";
	*agent->property( "Location" )->getVec3() = *chest->property( "Location" )->getVec3();
	chest->property( "Open" )->value = true;
	return "LockedChest with TreasureKey";
}

static std::string ExecuteAction( gie::World& world, gie::Agent* agent, const std::string& actionName )
{
	if( actionName == "Observe" )        return ExecuteObserve( world, agent );
	else if( actionName == "MoveTo" )    return ExecuteMoveTo( world, agent );
	else if( actionName == "Inspect" )   return ExecuteInspect( world, agent );
	else if( actionName == "PickUp" )    return ExecutePickUp( world, agent );
	else if( actionName == "ForgeKey" )  return ExecuteForgeKey( world, agent );
	else if( actionName == "OpenChest" ) return ExecuteOpenChest( world, agent );
	return "";
}

// ---------------------------------------------------------------------------
// Gameplay loop — single cycle and full loop
// ---------------------------------------------------------------------------

// Result of a single gameplay cycle
enum class CycleResult { Continued, GoalReached, Stuck };

// Execute one planning cycle: try primary goal, then explore, then stuck.
// Appends the cycle entry to g_GameplayLog.
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

	auto chest = FindEntityByName( world.context(), "LockedChest" );
	if( chest && *chest->property( "Open" )->getBool() )
	{
		g_GameplayLog.primaryGoalReached = true;
		return CycleResult::GoalReached;
	}

	Guid chestOpenPropGuid = chest ? chest->property( "Open" )->guid() : gie::NullGuid;
	Guid exploredPropGuid = agent->property( "ExploredNewArea" )->guid();

	int cycleNum = static_cast<int>( g_GameplayLog.cycles.size() ) + 1;
	GameplayCycleEntry entry;
	entry.cycle = cycleNum;
	entry.agentPosBefore = *agent->property( "Location" )->getVec3();
	entry.knownCountBefore = CountKnown( world.context() );

	// --- Try primary goal: open chest ---
	{
		gie::Goal primaryGoal{ world };
		if( chestOpenPropGuid != gie::NullGuid )
			primaryGoal.targets.emplace_back( chestOpenPropGuid, true );

		gie::Planner primaryPlanner{};
		RegisterActions( primaryPlanner );
		primaryPlanner.depthLimitMutator() = 8;
		primaryPlanner.logStepsMutator() = false;
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

			for( auto& name : entry.actionNames )
				entry.actionDetails.push_back( ExecuteAction( world, agent, name ) );

			entry.agentPosAfter = *agent->property( "Location" )->getVec3();
			entry.knownCountAfter = CountKnown( world.context() );
			entry.inventoryAfter = getInventoryNames();
			entry.plannerPtr = std::make_unique<gie::Planner>( std::move( primaryPlanner ) );
			g_GameplayLog.agentTrail.push_back( entry.agentPosAfter );
			g_GameplayLog.cycles.push_back( std::move( entry ) );

			chest = FindEntityByName( world.context(), "LockedChest" );
			if( chest && *chest->property( "Open" )->getBool() )
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

			for( auto& name : entry.actionNames )
				entry.actionDetails.push_back( ExecuteAction( world, agent, name ) );

			entry.agentPosAfter = *agent->property( "Location" )->getVec3();
			entry.knownCountAfter = CountKnown( world.context() );
			entry.inventoryAfter = getInventoryNames();
			entry.plannerPtr = std::make_unique<gie::Planner>( std::move( explorePlanner ) );
			g_GameplayLog.agentTrail.push_back( entry.agentPosAfter );
			g_GameplayLog.cycles.push_back( std::move( entry ) );
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

// Run all cycles until goal reached, stuck, or max cycles exceeded.
static void RunGameplayLoop( gie::World& world, gie::Agent* agent, gie::Planner& planner, bool useHeuristics = true )
{
	g_GameplayLog = {};
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
int treasureHunt( ExampleParameters& params )
{
	gie::Agent* agent = treasureHunt_world( params );

	// Register actions on the main planner (for visualization Plan! button)
	RegisterActions( params.planner );
	params.planner.depthLimitMutator() = 20;

	// Set primary goal on main planner
	auto chest = FindEntityByName( params.world.context(), "LockedChest" );
	if( chest )
		params.goal.targets.emplace_back( chest->property( "Open" )->guid(), true );

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
int treasureHuntValidateResult( std::string& failMsg )
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

	// --- Run with heuristics (A*) ---
	gie::World world{};
	gie::Planner planner{};
	gie::Goal goal{ world };
	ExampleParameters params{ world, planner, goal };

	VALIDATE( treasureHunt( params ) == 0, "treasureHunt() setup failed (heuristic)" );

	VALIDATE( g_GameplayLog.primaryGoalReached, "primary goal should be reached (heuristic)" );
	VALIDATE( g_GameplayLog.cycles.size() >= 2, "should have at least 2 planning cycles (heuristic)" );

	bool hasExplore = false;
	for( auto& c : g_GameplayLog.cycles )
		if( c.goalType == "Explore" ) { hasExplore = true; break; }
	VALIDATE( hasExplore, "should have at least one Explore cycle" );

	bool hasOpenChest = false;
	for( auto& c : g_GameplayLog.cycles )
		for( auto& a : c.actionNames )
			if( a == "OpenChest" ) { hasOpenChest = true; break; }
	VALIDATE( hasOpenChest, "plan should include OpenChest" );

	auto chest = FindEntityByName( world.context(), "LockedChest" );
	VALIDATE( chest != nullptr, "chest should exist" );
	VALIDATE( *chest->property( "Open" )->getBool(), "chest should be open (heuristic)" );

	auto [heuristicActions, heuristicSims] = captureResults();

	// --- Run without heuristics (BFS) ---
	gie::World world2{};
	gie::Planner planner2{};
	gie::Goal goal2{ world2 };
	ExampleParameters params2{ world2, planner2, goal2 };

	// treasureHunt calls RunGameplayLoop with default useHeuristics=true,
	// so we set up the world manually and call RunGameplayLoop with false.
	gie::Agent* agent2 = treasureHunt_world( params2 );
	RegisterActions( params2.planner );
	params2.planner.depthLimitMutator() = 20;
	auto chest2 = FindEntityByName( params2.world.context(), "LockedChest" );
	if( chest2 )
		params2.goal.targets.emplace_back( chest2->property( "Open" )->guid(), true );
	params2.planner.simulate( params2.goal, *agent2 );
	RunGameplayLoop( params2.world, agent2, params2.planner, false );

	VALIDATE( g_GameplayLog.primaryGoalReached, "primary goal should be reached (BFS)" );

	auto [bfsActions, bfsSims] = captureResults();

	// Both should end with OpenChest
	VALIDATE( !bfsActions.empty() && bfsActions.back() == "OpenChest", "BFS plan should end with OpenChest" );
	VALIDATE( !heuristicActions.empty() && heuristicActions.back() == "OpenChest", "A* plan should end with OpenChest" );

	// Log simulation counts for comparison (A* optimal termination may explore more
	// nodes than BFS first-found, but both must reach the goal)
	printf( "  simulations: A*=%zu BFS=%zu\n", heuristicSims, bfsSims );

	return 0;
}

// ---------------------------------------------------------------------------
// GL Draw — gameplay character trail and known entity markers
// ---------------------------------------------------------------------------
static void GLDrawFunc7( gie::World& world, gie::Planner& planner )
{
	glm::vec3 offset = -g_DrawingLimits.center;
	glm::vec3 scale = g_DrawingLimits.scale;

	// Draw agent trail (breadcrumb path)
	if( g_GameplayLog.agentTrail.size() > 1 )
	{
		glLineWidth( 2.0f );
		glColor3f( 0.2f, 0.8f, 0.2f );
		glBegin( GL_LINE_STRIP );
		for( auto& pos : g_GameplayLog.agentTrail )
		{
			glm::vec3 p = ( pos + offset ) * scale;
			glVertex3f( p.x, p.y, p.z );
		}
		glEnd();

		glPointSize( 8.0f );
		glColor3f( 0.3f, 1.0f, 0.3f );
		glBegin( GL_POINTS );
		for( auto& pos : g_GameplayLog.agentTrail )
		{
			glm::vec3 p = ( pos + offset ) * scale;
			glVertex3f( p.x, p.y, p.z );
		}
		glEnd();
		glLineWidth( 1.0f );
	}

	// Draw Known markers on entities
	auto knownSet = world.context().entityTagRegister().tagSet( "Known" );
	if( knownSet )
	{
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

			auto clueSet = world.context().entityTagRegister().tagSet( "Clue" );
			bool isClue = clueSet && clueSet->count( g );

			auto itemSet = world.context().entityTagRegister().tagSet( "Item" );
			bool isItem = itemSet && itemSet->count( g );

			if( isClue )       glColor3f( 1.0f, 1.0f, 0.2f );
			else if( isItem )  glColor3f( 0.2f, 0.8f, 1.0f );
			else if( isWp )    glColor3f( 0.6f, 0.6f, 0.6f );
			else               glColor3f( 1.0f, 0.5f, 0.0f );

			if( !isWp )
			{
				glBegin( GL_POINTS );
				glVertex3f( p.x, p.y, p.z );
				glEnd();
			}
		}
	}

	// Draw chest marker
	auto chest = FindEntityByName( world.context(), "LockedChest" );
	if( chest )
	{
		auto loc = chest->property( "Location" );
		if( loc )
		{
			glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
			bool open = *chest->property( "Open" )->getBool();
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

	// Draw forge marker
	auto forge = FindEntityByName( world.context(), "Blacksmith" );
	if( forge )
	{
		auto fKnown = world.context().entityTagRegister().tagSet( "Known" );
		if( fKnown && fKnown->count( forge->guid() ) )
		{
			auto loc = forge->property( "Location" );
			if( loc )
			{
				glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
				glColor3f( 1.0f, 0.6f, 0.0f );
				float s = 0.02f;
				glBegin( GL_TRIANGLES );
				glVertex3f( p.x, p.y - s, p.z ); glVertex3f( p.x - s, p.y, p.z ); glVertex3f( p.x + s, p.y, p.z );
				glVertex3f( p.x, p.y + s, p.z ); glVertex3f( p.x - s, p.y, p.z ); glVertex3f( p.x + s, p.y, p.z );
				glEnd();
			}
		}
	}
}

// ---------------------------------------------------------------------------
// ImGui panel — gameplay log and world state
// ---------------------------------------------------------------------------
static void ImGuiFunc7( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimGuid )
{
	ImGui::TextUnformatted( "Treasure Hunt - Gameplay Loop (Example 7)" );
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
		ImGui::Text( "Known locations: %d / 8", known );

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

	// Show chest status
	auto chest = FindEntityByName( *ctx, "LockedChest" );
	auto knownSet = ctx->entityTagRegister().tagSet( "Known" );
	if( chest && knownSet && knownSet->count( chest->guid() ) )
	{
		bool open = *const_cast<gie::Entity*>( chest )->property( "Open" )->getBool();
		ImGui::Text( "Chest: %s", open ? "OPEN" : "Locked" );
	}
	else
	{
		ImGui::Text( "Chest: Not yet discovered" );
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
			"Each cycle's planner and simulation tree will be recorded." );
		return;
	}

	// Gameplay log
	if( ImGui::CollapsingHeader( "Gameplay Log", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		if( g_GameplayLog.primaryGoalReached )
			ImGui::TextColored( ImVec4( 0.4f, 1.0f, 0.4f, 1.0f ), "Goal reached: YES" );
		else
			ImGui::Text( "Goal reached: No" );
		ImGui::Text( "Total cycles: %zu", g_GameplayLog.cycles.size() );
		ImGui::Separator();

		for( size_t i = 0; i < g_GameplayLog.cycles.size(); i++ )
		{
			auto& c = g_GameplayLog.cycles[i];

			// Color-code by goal type
			ImVec4 color = ( c.goalType == "Primary" ) ? ImVec4( 0.4f, 1.0f, 0.4f, 1.0f )
				: ( c.goalType == "Explore" ) ? ImVec4( 0.4f, 0.6f, 1.0f, 1.0f )
				: ImVec4( 1.0f, 0.4f, 0.4f, 1.0f );
			ImGui::PushStyleColor( ImGuiCol_Text, color );

			char label[128];
			std::snprintf( label, sizeof( label ), "Cycle %d [%s] %zu sims, %zu actions###cycle%zu",
				c.cycle, c.goalType.c_str(), c.simulationCount, c.actionNames.size(), i );

			bool nodeOpen = ImGui::TreeNodeEx( label, ImGuiTreeNodeFlags_DefaultOpen );
			ImGui::PopStyleColor();

			if( nodeOpen )
			{
				// Executed actions list with details
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

					if( isLast && g_GameplayLog.primaryGoalReached )
						ImGui::PopStyleColor();
				}
				ImGui::Unindent();

				// Cycle metadata
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

				// Simulation tree (from preserved planner)
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

	// Run / Step buttons (shown when step execution is active and goal not yet reached)
	if( stepExecution && !finished )
	{
		ImGui::Separator();
		float buttonWidth = ( ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x ) * 0.5f;

		ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.2f, 0.6f, 0.2f, 1.0f ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.3f, 0.8f, 0.3f, 1.0f ) );
		if( ImGui::Button( "Run", ImVec2( buttonWidth, 30.f ) ) )
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
		if( ImGui::Button( "Step", ImVec2( buttonWidth, 30.f ) ) )
		{
			auto* agentPtr = planner.agent();
			if( agentPtr )
				RunGameplayCycle( world, agentPtr );
		}
		ImGui::PopStyleColor( 2 );
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
}
