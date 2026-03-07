#include <functional>
#include <array>
#include <algorithm>
#include <random>
#include <set>

#include <goapie.h>

#include "example.h"
#include "waypoint_navigation.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

// Discovery Example (Example 7)
// Detective treasure hunt on an island with partial observability.
// The agent starts knowing only its current location. It must explore,
// observe surroundings, inspect objects for clues, gather tools, and
// eventually locate and open a hidden treasure chest.
//
// Key concepts demonstrated:
//   - Partial world knowledge: agent discovers entities at runtime
//   - Primary/secondary goals with fallback when primary is unreachable
//   - Replanning after world changes (new entities discovered)
//   - False clues that lead to dead ends
//   - Tool crafting/usage gating (need key forged from ore to open chest)

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
	bool showFullMap{ false }; // debug: reveal all waypoints in visualization
};
static DiscoveryToggles g_Toggles{};

// Forward declarations
static void ImGuiFunc7( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimulationGuid );

// ---------------------------------------------------------------------------
// World layout — an island with 8 areas connected by paths:
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
//
// Discovery layers:
//   Beach: starting point, observe reveals Jungle
//   Jungle: observe reveals Ruins + OldMap (false clue -> Swamp)
//   Ruins: observe reveals Cliff + Inscription + IronOre
//   Swamp: dead end, observe reveals nothing useful (MuddyNote = false clue)
//   Cliff: observe reveals Waterfall + Village
//   Waterfall: observe reveals HiddenCave entrance
//   HiddenCave: contains the LockedChest (primary goal target)
//   Village: has Blacksmith who forges TreasureKey from IronOre
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

// Get all waypoint guids agent knows about (tagged "Known" + "Waypoint")
static std::vector<Guid> getKnownWaypoints( const gie::Blackboard& ctx )
{
	std::vector<Guid> result;
	auto wpSet = ctx.entityTagRegister().tagSet( "Waypoint" );
	if( !wpSet )
	{
		// check world context
		return result;
	}
	auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
	if( !knownSet ) return result;
	for( auto g : *wpSet )
	{
		if( knownSet->count( g ) ) result.push_back( g );
	}
	return result;
}

// Broader version: get all waypoints (known or not) for pathfinding in simulation
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

// Heuristic for exploration goal: fewer known waypoints = higher heuristic
static float EstimateExploreHeuristic( const gie::Simulation& sim, const gie::Entity* agentEnt )
{
	if( !agentEnt ) return 0.f;
	// estimate: each unknown area costs ~10 to discover
	int known = CountKnown( sim.context() );
	int total = 8; // total waypoints in the world
	int unknown = std::max( 0, total - known );
	return static_cast<float>( unknown ) * 5.f;
}

// Heuristic for treasure goal
static float EstimateTreasureHeuristic( const gie::Simulation& sim, const gie::Entity* agentEnt )
{
	if( !agentEnt ) return 0.f;

	// If chest is open, done
	auto chest = FindEntityByName( sim.context(), "LockedChest" );
	if( chest )
	{
		auto openProp = chest->property( "Open" );
		if( openProp && *openProp->getBool() ) return 0.f;
	}

	float h = 0.f;

	// Penalty if chest not yet discovered
	if( !chest ) h += 30.f;

	// Penalty if no key
	if( !HasItemWithInfo( sim.context(), agentEnt->guid(), "TreasureKeyInfo" ) )
		h += 20.f;

	// Penalty if no ore (needed to forge key)
	if( !HasItemWithInfo( sim.context(), agentEnt->guid(), "IronOreInfo" ) )
		h += 10.f;

	// Distance to chest if known
	if( chest )
	{
		auto agentLoc = agentEnt->property( "Location" );
		auto chestLoc = chest->property( "Location" );
		if( agentLoc && chestLoc )
		{
			h += glm::distance( *agentLoc->getVec3(), *chestLoc->getVec3() ) * 0.1f;
		}
	}

	return h;
}

// ---------------------------------------------------------------------------
// World setup
// ---------------------------------------------------------------------------
gie::Agent* treasureHunt_world( ExampleParameters& params )
{
	gie::World& world = params.world;
	params.imGuiDrawFunc = &ImGuiFunc7;

	auto selectableTag = H( "Selectable" );
	auto drawTag = H( "Draw" );

	// Agent
	auto agent = world.createAgent( "Explorer" );
	world.context().entityTagRegister().tag( agent, { H( "Agent" ) } );
	agent->createProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } ); // Beach
	agent->createProperty( "Inventory", gie::Property::GuidVector{} );
	agent->createProperty( "DiscoveryCount", 1.f ); // starts knowing Beach

	// Archetypes
	if( auto* a = world.createArchetype( "Waypoint" ) )
	{
		a->addTag( selectableTag );
		a->addTag( "Waypoint" );
		a->addTag( drawTag );
		a->addProperty( "Location", glm::vec3{ 0.f } );
		a->addProperty( "Links", gie::Property::GuidVector{} );
		// Reveals: list of entity GUIDs that become Known when this waypoint is Observed
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
		// RevealTarget: guid of entity that inspecting this clue reveals
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
		{ "WP_Beach",       {  0.f,   0.f, 0.f } },   // 0
		{ "WP_Jungle",      {  0.f,  15.f, 0.f } },   // 1
		{ "WP_Ruins",       { 20.f,  30.f, 0.f } },   // 2
		{ "WP_Swamp",       { 35.f,   5.f, 0.f } },   // 3
		{ "WP_Cliff",       { 40.f,  25.f, 0.f } },   // 4
		{ "WP_Waterfall",   { 55.f,  10.f, 0.f } },   // 5
		{ "WP_HiddenCave",  { 65.f,   0.f, 0.f } },   // 6
		{ "WP_Village",     { 60.f,  40.f, 0.f } },   // 7
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

	// Bidirectional links
	auto link = [&]( size_t a, size_t b )
	{
		wpLinks[a]->push_back( wps[b]->guid() );
		wpLinks[b]->push_back( wps[a]->guid() );
	};

	link( 0, 1 ); // Beach <-> Jungle
	link( 1, 2 ); // Jungle <-> Ruins
	link( 1, 3 ); // Jungle <-> Swamp (false clue path)
	link( 2, 4 ); // Ruins <-> Cliff
	link( 4, 5 ); // Cliff <-> Waterfall
	link( 5, 6 ); // Waterfall <-> HiddenCave
	link( 4, 7 ); // Cliff <-> Village

	// Beach is Known at start; everything else starts unknown
	world.context().entityTagRegister().tag( wps[0], { H( "Known" ) } );

	// -- Info entities --
	auto makeInfo = [&]( const char* infoName ) -> gie::Entity*
	{
		auto e = world.createEntity( infoName );
		e->createProperty( "Name", infoName );
		world.context().entityTagRegister().tag( e, { H( "Info" ) } );
		return e;
	};

	auto infoOldMap     = makeInfo( "OldMapInfo" );        // false clue -> points to Swamp
	auto infoMachete    = makeInfo( "MacheteInfo" );       // tool (not strictly needed, flavor)
	auto infoIronOre    = makeInfo( "IronOreInfo" );       // required for key forging
	auto infoMuddyNote  = makeInfo( "MuddyNoteInfo" );    // false clue in Swamp
	auto infoTreasureKey = makeInfo( "TreasureKeyInfo" );  // forged at Village blacksmith

	// -- Items --
	auto placeItem = [&]( const char* itemName, gie::Entity* info, glm::vec3 pos ) -> gie::Entity*
	{
		auto e = world.createEntity( itemName );
		world.context().entityTagRegister().tag( e, { H( "Item" ) } );
		e->createProperty( "Location", pos );
		e->createProperty( "Info", info ? info->guid() : gie::NullGuid );
		return e;
	};

	// OldMap in Jungle (false clue item — inspecting reveals Swamp waypoint)
	auto oldMap = placeItem( "OldMap", infoOldMap, wpDefs[1].pos );

	// Machete in Jungle (flavor item, collectable)
	placeItem( "Machete", infoMachete, wpDefs[1].pos + glm::vec3{ 3.f, 0.f, 0.f } );

	// IronOre in Ruins (needed to forge key)
	auto ironOre = placeItem( "IronOre", infoIronOre, wpDefs[2].pos );

	// MuddyNote in Swamp (dead-end false clue)
	auto muddyNote = placeItem( "MuddyNote", infoMuddyNote, wpDefs[3].pos );

	// -- Clues --
	// OldMap clue: inspecting it reveals Swamp (false clue)
	auto clueOldMap = world.createEntity( "ClueOldMap" );
	world.context().entityTagRegister().tag( clueOldMap, { H( "Clue" ), drawTag, selectableTag } );
	clueOldMap->createProperty( "Location", wpDefs[1].pos );
	clueOldMap->createProperty( "Inspected", false );
	clueOldMap->createProperty( "RevealTarget", wps[3]->guid() ); // reveals Swamp
	clueOldMap->createProperty( "IsFalseClue", true );

	// Inscription in Ruins: inspecting reveals Waterfall (true clue toward cave)
	auto clueInscription = world.createEntity( "ClueInscription" );
	world.context().entityTagRegister().tag( clueInscription, { H( "Clue" ), drawTag, selectableTag } );
	clueInscription->createProperty( "Location", wpDefs[2].pos + glm::vec3{ 2.f, 2.f, 0.f } );
	clueInscription->createProperty( "Inspected", false );
	clueInscription->createProperty( "RevealTarget", wps[5]->guid() ); // reveals Waterfall
	clueInscription->createProperty( "IsFalseClue", false );

	// MuddyNote clue in Swamp: inspecting reveals nothing useful
	auto clueMuddyNote = world.createEntity( "ClueMuddyNote" );
	world.context().entityTagRegister().tag( clueMuddyNote, { H( "Clue" ), drawTag, selectableTag } );
	clueMuddyNote->createProperty( "Location", wpDefs[3].pos );
	clueMuddyNote->createProperty( "Inspected", false );
	clueMuddyNote->createProperty( "RevealTarget", gie::NullGuid ); // dead end
	clueMuddyNote->createProperty( "IsFalseClue", true );

	// -- Observe reveals setup --
	// Observing at Beach reveals: Jungle waypoint + nothing else
	wpReveals[0]->push_back( wps[1]->guid() );

	// Observing at Jungle reveals: Ruins + ClueOldMap + OldMap + Machete
	wpReveals[1]->push_back( wps[2]->guid() );
	wpReveals[1]->push_back( clueOldMap->guid() );
	wpReveals[1]->push_back( oldMap->guid() );

	// Observing at Ruins reveals: Cliff + ClueInscription + IronOre
	wpReveals[2]->push_back( wps[4]->guid() );
	wpReveals[2]->push_back( clueInscription->guid() );
	wpReveals[2]->push_back( ironOre->guid() );

	// Observing at Swamp reveals: ClueMuddyNote + MuddyNote (dead end)
	wpReveals[3]->push_back( clueMuddyNote->guid() );
	wpReveals[3]->push_back( muddyNote->guid() );

	// Observing at Cliff reveals: Waterfall + Village
	wpReveals[4]->push_back( wps[5]->guid() );
	wpReveals[4]->push_back( wps[7]->guid() );

	// Observing at Waterfall reveals: HiddenCave
	wpReveals[5]->push_back( wps[6]->guid() );

	// Observing at HiddenCave reveals: LockedChest (created below)
	// Observing at Village reveals: Blacksmith forge

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

	// -- Goal: open the chest --
	// Primary goal target
	auto chestOpenProp = chest->property( "Open" );
	params.goal.targets.emplace_back( chestOpenProp->guid(), true );

	return agent;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
static int treasureHunt_actions( ExampleParameters& params, gie::Agent* agent )
{
	gie::World& world = params.world;
	gie::Planner& planner = params.planner;
	gie::Goal& goal = params.goal;

	// Dummy action classes
	DEFINE_DUMMY_ACTION_CLASS( Observe )
	DEFINE_DUMMY_ACTION_CLASS( MoveTo )
	DEFINE_DUMMY_ACTION_CLASS( Inspect )
	DEFINE_DUMMY_ACTION_CLASS( PickUp )
	DEFINE_DUMMY_ACTION_CLASS( ForgeKey )
	DEFINE_DUMMY_ACTION_CLASS( OpenChest )

	// -----------------------------------------------------------------------
	// Observe: stand at a Known waypoint and reveal what it shows
	// -----------------------------------------------------------------------
	class ObserveSimulator : public gie::ActionSimulator
	{
	public:
		using gie::ActionSimulator::ActionSimulator;
		gie::StringHash hash() const override { return H( "Observe" ); }

		bool evaluate( gie::EvaluateSimulationParams params ) const override
		{
			params.addDebugMessage( "Observe::evaluate" );
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			if( !agentEnt ) return false;
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			// Find the waypoint the agent is standing on
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
				if( glm::distance( agentLoc, *loc->getVec3() ) > 1.f ) continue;

				// Check if this waypoint has unrevealed entities
				auto reveals = wp->property( "Reveals" );
				if( !reveals ) continue;
				auto revArr = reveals->getGuidArray();
				if( !revArr || revArr->empty() ) continue;

				for( Guid rg : *revArr )
				{
					if( !knownSet->count( rg ) )
					{
						params.addDebugMessage( "  Waypoint has unrevealed entities -> TRUE" );
						return true;
					}
				}
			}
			params.addDebugMessage( "  Nothing new to observe -> FALSE" );
			return false;
		}

		bool simulate( gie::SimulateSimulationParams params ) const override
		{
			params.addDebugMessage( "Observe::simulate" );
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			auto wpSet = params.simulation.tagSet( "Waypoint" );
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			if( !wpSet || !knownSet ) return false;

			bool revealed = false;
			for( auto g : *wpSet )
			{
				if( !knownSet->count( g ) ) continue;
				auto wp = ctx.entity( g );
				if( !wp ) continue;
				auto loc = wp->property( "Location" );
				if( !loc ) continue;
				if( glm::distance( agentLoc, *loc->getVec3() ) > 1.f ) continue;

				auto reveals = wp->property( "Reveals" );
				if( !reveals ) continue;
				auto revArr = reveals->getGuidArray();
				if( !revArr ) continue;

				for( Guid rg : *revArr )
				{
					if( !knownSet->count( rg ) )
					{
						// Mark entity as Known
						auto ent = ctx.entity( rg );
						if( ent )
						{
							ctx.entityTagRegister().tag( ent, { H( "Known" ) } );
							params.addDebugMessage( "  Revealed: " + std::string( gie::stringRegister().get( ent->nameHash() ) ) );
							revealed = true;
						}
					}
				}
			}

			if( !revealed ) return false;

			// Update discovery count
			auto dcProp = agentEnt->property( "DiscoveryCount" );
			if( dcProp )
			{
				int known = CountKnown( ctx );
				dcProp->value = static_cast<float>( known );
			}

			SetSimulationCost( params.simulation, 0.f, 2.f );
			if( auto a = std::make_shared< ObserveAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
			return false;
		}

		void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
		{
			auto agentEnt = params.simulation.context().entity( params.agent.guid() );
			params.simulation.heuristic.value = EstimateTreasureHeuristic( params.simulation, agentEnt );
		}
	};

	// -----------------------------------------------------------------------
	// MoveTo: travel to a Known waypoint that is linked from current position
	// -----------------------------------------------------------------------
	class MoveToSimulator : public gie::ActionSimulator
	{
	public:
		using gie::ActionSimulator::ActionSimulator;
		gie::StringHash hash() const override { return H( "MoveTo" ); }

		bool evaluate( gie::EvaluateSimulationParams params ) const override
		{
			params.addDebugMessage( "MoveTo::evaluate" );
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			if( !agentEnt ) return false;

			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			auto wpSet = params.simulation.tagSet( "Waypoint" );
			if( !knownSet || !wpSet ) return false;

			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			// Find known waypoints the agent is NOT currently at
			for( auto g : *wpSet )
			{
				if( !knownSet->count( g ) ) continue;
				auto wp = ctx.entity( g );
				if( !wp ) continue;
				auto loc = wp->property( "Location" );
				if( !loc ) continue;
				if( glm::distance( agentLoc, *loc->getVec3() ) > 1.f )
				{
					params.addDebugMessage( "  Reachable known waypoint exists -> TRUE" );
					return true;
				}
			}
			params.addDebugMessage( "  No known waypoints to move to -> FALSE" );
			return false;
		}

		bool simulate( gie::SimulateSimulationParams params ) const override
		{
			params.addDebugMessage( "MoveTo::simulate" );
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			auto wpSet = params.simulation.tagSet( "Waypoint" );
			if( !knownSet || !wpSet ) return false;

			// Pick the nearest known waypoint that has unrevealed content
			// (prefer waypoints with Reveals that haven't been fully explored)
			// Fallback: nearest unvisited known waypoint
			auto allWp = getAllWaypoints( params.simulation );

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
				if( dist < 1.f ) continue; // skip current location

				// Prefer waypoints that still have unrevealed content
				float score = dist;
				auto reveals = wp->property( "Reveals" );
				if( reveals )
				{
					auto revArr = reveals->getGuidArray();
					if( revArr )
					{
						bool hasUnrevealed = false;
						for( Guid rg : *revArr )
						{
							if( !knownSet->count( rg ) ) { hasUnrevealed = true; break; }
						}
						if( hasUnrevealed ) score -= 50.f; // strong preference for unexplored
					}
				}

				if( score < bestScore )
				{
					bestScore = score;
					bestTarget = wp;
				}
			}

			if( !bestTarget ) { params.addDebugMessage( "  No target -> FALSE" ); return false; }

			float len = MoveAgentAlongPath( params, agentLoc, bestTarget, allWp );
			SetSimulationCost( params.simulation, len, 1.f );
			if( auto a = std::make_shared< MoveToAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
			return false;
		}

		void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
		{
			auto agentEnt = params.simulation.context().entity( params.agent.guid() );
			params.simulation.heuristic.value = EstimateTreasureHeuristic( params.simulation, agentEnt );
		}
	};

	// -----------------------------------------------------------------------
	// Inspect: examine a Known Clue entity to reveal its target
	// -----------------------------------------------------------------------
	class InspectSimulator : public gie::ActionSimulator
	{
	public:
		using gie::ActionSimulator::ActionSimulator;
		gie::StringHash hash() const override { return H( "Inspect" ); }

		bool evaluate( gie::EvaluateSimulationParams params ) const override
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
					params.addDebugMessage( "  Uninspected known clue found -> TRUE" );
					return true;
				}
			}
			params.addDebugMessage( "  No uninspected clues -> FALSE" );
			return false;
		}

		bool simulate( gie::SimulateSimulationParams params ) const override
		{
			params.addDebugMessage( "Inspect::simulate" );
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			auto clueSet = params.simulation.tagSet( "Clue" );
			if( !knownSet || !clueSet ) return false;

			// Find nearest uninspected known clue
			auto allWp = getAllWaypoints( params.simulation );
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

			// Move to clue
			float len = MoveAgentAlongPath( params, agentLoc, bestClue, allWp );

			// Mark inspected
			bestClue->property( "Inspected" )->value = true;

			// Reveal target
			auto revealTarget = bestClue->property( "RevealTarget" );
			if( revealTarget && *revealTarget->getGuid() != gie::NullGuid )
			{
				Guid targetGuid = *revealTarget->getGuid();
				auto targetEnt = ctx.entity( targetGuid );
				if( targetEnt )
				{
					ctx.entityTagRegister().tag( targetEnt, { H( "Known" ) } );
					params.addDebugMessage( "  Inspected clue revealed: " + std::string( gie::stringRegister().get( targetEnt->nameHash() ) ) );
				}
			}
			else
			{
				params.addDebugMessage( "  Inspected clue: dead end (nothing revealed)" );
			}

			SetSimulationCost( params.simulation, len, 3.f );
			if( auto a = std::make_shared< InspectAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
			return false;
		}

		void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
		{
			auto agentEnt = params.simulation.context().entity( params.agent.guid() );
			params.simulation.heuristic.value = EstimateTreasureHeuristic( params.simulation, agentEnt );
		}
	};

	// -----------------------------------------------------------------------
	// PickUp: collect a Known Item not already in inventory
	// -----------------------------------------------------------------------
	class PickUpSimulator : public gie::ActionSimulator
	{
	public:
		using gie::ActionSimulator::ActionSimulator;
		gie::StringHash hash() const override { return H( "PickUp" ); }

		bool evaluate( gie::EvaluateSimulationParams params ) const override
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
		}

		bool simulate( gie::SimulateSimulationParams params ) const override
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
			params.addDebugMessage( "  Picked up: " + std::string( gie::stringRegister().get( best->nameHash() ) ) );

			SetSimulationCost( params.simulation, len, 1.f );
			if( auto a = std::make_shared< PickUpAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
			return false;
		}

		void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
		{
			auto agentEnt = params.simulation.context().entity( params.agent.guid() );
			params.simulation.heuristic.value = EstimateTreasureHeuristic( params.simulation, agentEnt );
		}
	};

	// -----------------------------------------------------------------------
	// ForgeKey: at the Blacksmith, convert IronOre into TreasureKey
	// -----------------------------------------------------------------------
	class ForgeKeySimulator : public gie::ActionSimulator
	{
	public:
		using gie::ActionSimulator::ActionSimulator;
		gie::StringHash hash() const override { return H( "ForgeKey" ); }

		bool evaluate( gie::EvaluateSimulationParams params ) const override
		{
			params.addDebugMessage( "ForgeKey::evaluate" );
			auto& ctx = params.simulation.context();

			// Need Known Blacksmith
			auto forge = FindEntityByName( ctx, "Blacksmith" );
			if( !forge ) { params.addDebugMessage( "  Blacksmith not known -> FALSE" ); return false; }
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			if( !knownSet || !knownSet->count( forge->guid() ) ) { params.addDebugMessage( "  Blacksmith not known -> FALSE" ); return false; }

			// Need IronOre in inventory
			if( !HasItemWithInfo( ctx, params.agent.guid(), "IronOreInfo" ) )
			{ params.addDebugMessage( "  No IronOre -> FALSE" ); return false; }

			// Must not already have the key
			if( HasItemWithInfo( ctx, params.agent.guid(), "TreasureKeyInfo" ) )
			{ params.addDebugMessage( "  Already have key -> FALSE" ); return false; }

			params.addDebugMessage( "  Can forge key -> TRUE" );
			return true;
		}

		bool simulate( gie::SimulateSimulationParams params ) const override
		{
			params.addDebugMessage( "ForgeKey::simulate" );
			auto& ctx = params.simulation.context();
			auto agentEnt = ctx.entity( params.agent.guid() );
			glm::vec3 agentLoc = *agentEnt->property( "Location" )->getVec3();

			auto forge = FindEntityByName( ctx, "Blacksmith" );
			if( !forge ) return false;

			auto allWp = getAllWaypoints( params.simulation );
			float len = MoveAgentAlongPath( params, agentLoc, forge, allWp );

			// Create the TreasureKey item and add to inventory
			// Find TreasureKeyInfo
			auto keyInfo = FindEntityByName( ctx, "TreasureKeyInfo" );
			if( !keyInfo ) return false;

			auto keyItem = ctx.createEntity( "TreasureKey" );
			ctx.entityTagRegister().tag( keyItem, { H( "Item" ), H( "Known" ) } );
			keyItem->createProperty( "Location", *forge->property( "Location" )->getVec3() );
			keyItem->createProperty( "Info", keyInfo->guid() );

			auto inv = agentEnt->property( "Inventory" )->getGuidArray();
			inv->push_back( keyItem->guid() );

			params.addDebugMessage( "  Forged TreasureKey from IronOre" );
			SetSimulationCost( params.simulation, len, 5.f );
			if( auto a = std::make_shared< ForgeKeyAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
			return false;
		}

		void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
		{
			auto agentEnt = params.simulation.context().entity( params.agent.guid() );
			params.simulation.heuristic.value = EstimateTreasureHeuristic( params.simulation, agentEnt );
		}
	};

	// -----------------------------------------------------------------------
	// OpenChest: open the LockedChest with the TreasureKey
	// -----------------------------------------------------------------------
	class OpenChestSimulator : public gie::ActionSimulator
	{
	public:
		using gie::ActionSimulator::ActionSimulator;
		gie::StringHash hash() const override { return H( "OpenChest" ); }

		bool evaluate( gie::EvaluateSimulationParams params ) const override
		{
			params.addDebugMessage( "OpenChest::evaluate" );
			auto& ctx = params.simulation.context();

			auto chest = FindEntityByName( ctx, "LockedChest" );
			if( !chest ) { params.addDebugMessage( "  Chest not known -> FALSE" ); return false; }
			auto knownSet = ctx.entityTagRegister().tagSet( "Known" );
			if( !knownSet || !knownSet->count( chest->guid() ) ) { params.addDebugMessage( "  Chest not known -> FALSE" ); return false; }
			if( *chest->property( "Open" )->getBool() ) { params.addDebugMessage( "  Already open -> FALSE" ); return false; }

			// Need TreasureKey
			if( !HasItemWithInfo( ctx, params.agent.guid(), "TreasureKeyInfo" ) )
			{ params.addDebugMessage( "  No TreasureKey -> FALSE" ); return false; }

			params.addDebugMessage( "  Can open chest -> TRUE" );
			return true;
		}

		bool simulate( gie::SimulateSimulationParams params ) const override
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
			params.addDebugMessage( "  Opened the treasure chest!" );

			SetSimulationCost( params.simulation, len, 2.f );
			if( auto a = std::make_shared< OpenChestAction >() ) { params.simulation.actions.emplace_back( a ); return true; }
			return false;
		}

		void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
		{
			auto agentEnt = params.simulation.context().entity( params.agent.guid() );
			params.simulation.heuristic.value = EstimateTreasureHeuristic( params.simulation, agentEnt );
		}
	};

	// Register action set entries
	DEFINE_ACTION_SET_ENTRY( Observe )
	DEFINE_ACTION_SET_ENTRY( MoveTo )
	DEFINE_ACTION_SET_ENTRY( Inspect )
	DEFINE_ACTION_SET_ENTRY( PickUp )
	DEFINE_ACTION_SET_ENTRY( ForgeKey )
	DEFINE_ACTION_SET_ENTRY( OpenChest )

	planner.addActionSetEntry< ObserveActionSetEntry >( H( "Observe" ) );
	planner.addActionSetEntry< MoveToActionSetEntry >( H( "MoveTo" ) );
	planner.addActionSetEntry< InspectActionSetEntry >( H( "Inspect" ) );
	planner.addActionSetEntry< PickUpActionSetEntry >( H( "PickUp" ) );
	planner.addActionSetEntry< ForgeKeyActionSetEntry >( H( "ForgeKey" ) );
	planner.addActionSetEntry< OpenChestActionSetEntry >( H( "OpenChest" ) );

	// Deeper search needed for multi-stage discovery
	planner.depthLimitMutator() = 20;

	// Set initial simulation root
	planner.simulate( goal, *agent );

	return 0;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int treasureHunt( ExampleParameters& params )
{
	gie::Agent* agent = treasureHunt_world( params );
	return treasureHunt_actions( params, agent );
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
int treasureHuntValidateResult( std::string& failMsg )
{
	gie::World world{};
	gie::Planner planner{};
	gie::Goal goal{ world };
	ExampleParameters params{ world, planner, goal };

	VALIDATE( treasureHunt( params ) == 0, "treasureHunt() setup failed" );

	// Run planner with heuristic
	planner.plan( true );

	auto& planned = planner.planActions();

	// The plan should not be empty — the planner must find a path to the chest
	VALIDATE( !planned.empty(), "planned actions should not be empty" );

	// The last action (backtrack order: leaf first) should be OpenChest
	VALIDATE_STR_EQ( planned[0]->name(), "OpenChest", "first backtracked action should be OpenChest" );

	// ForgeKey should appear somewhere in the plan
	bool hasForge = false;
	for( auto& a : planned )
	{
		if( a->name() == "ForgeKey" ) { hasForge = true; break; }
	}
	VALIDATE( hasForge, "plan should include ForgeKey" );

	// Should have at least some Observe actions (discovery is required)
	int observeCount = 0;
	for( auto& a : planned )
	{
		if( a->name() == "Observe" ) observeCount++;
	}
	VALIDATE( observeCount >= 2, "plan should include at least 2 Observe actions" );

	return 0;
}

// ---------------------------------------------------------------------------
// ImGui panel
// ---------------------------------------------------------------------------
static void ImGuiFunc7( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimulationGuid )
{
	ImGui::TextUnformatted( "Treasure Hunt - Discovery (Example 7)" );
	ImGui::Separator();

	if( ImGui::CollapsingHeader( "Settings", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		ImGui::SliderFloat( "Travel Cost Weight", &g_Toggles.travelCostWeight, 0.1f, 3.0f );
		ImGui::Checkbox( "Show Full Map (debug)", &g_Toggles.showFullMap );
	}

	// Show agent state
	const auto* sim = planner.simulation( selectedSimulationGuid );
	const gie::Blackboard* ctx = sim ? &sim->context() : &world.context();
	auto agentEnt = ctx->entity( planner.agent()->guid() );
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
					ImGui::Text( "- %s", gie::stringRegister().get( e->nameHash() ) );
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
						ImGui::Text( "  %s (%.0f, %.0f)", nm, loc->getVec3()->x, loc->getVec3()->y );
					else
						ImGui::Text( "  %s", nm );
				}
			}
		}
	}

	if( ImGui::Button( "Reset Agent" ) )
	{
		auto agent = planner.agent();
		if( agent )
		{
			if( auto p = agent->property( "Location" ) ) *p->getVec3() = glm::vec3{ 0.f, 0.f, 0.f };
			if( auto p = agent->property( "Inventory" ) ) p->getGuidArray()->clear();
			if( auto p = agent->property( "DiscoveryCount" ) ) p->value = 1.f;
		}
	}
}
