#include <functional>
#include <array>
#include <algorithm>

#include <goapie.h>

#include "example.h"
#include "waypoint_navigation.h"
#include "gameplay_common.h"
#include "visualization.h"

#include <limits>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

static inline float clampf( float v, float lo, float hi ) { return std::max( lo, std::min( v, hi ) ); }

// Add description function
const char* survivalOnHillDescription()
{
    return "Survival example demonstrating energy, hunger and thirst management while gathering resources.";
}

// Helper to apply travel-based survival deltas. Kept at file scope so inner simulator classes can use it.
static inline void ApplyTravelAndBaseDeltas(
    gie::Entity* agent,
    float baseEnergyCost,
    float baseHungerGain,
    float baseThirstGain,
    float pathLen,
    float energyPerPath,
    float hungerPerPath,
    float thirstPerPath,
    float maxEnergy,
    float maxHunger,
    float maxThirst,
    gie::SimulateSimulationParams& params,
    const char* scope )
{
    auto e = agent->property( "Energy" );
    auto h = agent->property( "Hunger" );
    auto t = agent->property( "Thirst" );
    const float energyBefore = e ? *e->getFloat() : 0.f;
    const float hungerBefore = h ? *h->getFloat() : 0.f;
    const float thirstBefore = t ? *t->getFloat() : 0.f;

    const float energyCost = baseEnergyCost + energyPerPath * pathLen;
    const float hungerGain = baseHungerGain + hungerPerPath * pathLen;
    const float thirstGain = baseThirstGain + thirstPerPath * pathLen;

    if( e ) e->value = clampf( *e->getFloat() - energyCost, 0.f, maxEnergy );
    if( h ) h->value = clampf( *h->getFloat() + hungerGain, 0.f, maxHunger );
    if( t ) t->value = clampf( *t->getFloat() + thirstGain, 0.f, maxThirst );

    params.addDebugMessage( std::string( scope ) + " travel/base deltas:" );
    params.addDebugMessage( "  PathLen: " + std::to_string( pathLen ) );
    params.addDebugMessage( "  Energy: " + std::to_string( energyBefore ) + " -> " + std::to_string( e ? *e->getFloat() : energyBefore ) );
    params.addDebugMessage( "  Hunger: " + std::to_string( hungerBefore ) + " -> " + std::to_string( h ? *h->getFloat() : hungerBefore ) );
    params.addDebugMessage( "  Thirst: " + std::to_string( thirstBefore ) + " -> " + std::to_string( t ? *t->getFloat() : thirstBefore ) );
}

void printSimulatedActions( const gie::Planner& planner );
float remapRange( float source, float sourceFrom, float sourceTo, float targetFrom, float targetTo );
static void ImGuiFunc5( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimulationGuid );
static void GLDrawFunc5( gie::World& world, gie::Planner& planner );
static void RegisterActions( gie::Planner& planner );
static CycleResult RunGameplayCycle( gie::World& world, gie::Agent* agent );
static void RunGameplayLoop( gie::World& world, gie::Agent* agent, gie::Planner& planner );

// Gameplay state
static GameplayLog g_GameplayLog;
static std::vector<std::vector<std::vector<glm::vec3>>> g_CycleActionPaths;
static std::vector<glm::vec3> g_LastActionPath;

// Survival constants (file scope so RegisterActions, Execute functions, and gameplay loop can use them)
static constexpr float kMaxEnergy = 100.f;
static constexpr float kMaxHunger = 100.f; // higher == more hungry
static constexpr float kMaxThirst = 100.f; // higher == more thirsty
static constexpr float kMinEnergyToAct = 20.f;
static constexpr float kHungerHigh = 70.f;
static constexpr float kThirstHigh = 70.f;
static constexpr float kEnergyPerPath = 0.25f;  // energy consumed per path length unit
static constexpr float kHungerPerPath = 0.15f;  // hunger increases per path unit
static constexpr float kThirstPerPath = 0.2f;   // thirst increases per path unit
static constexpr float kAxePrice = 15.f;
static constexpr float kWorkSalary = 20.f;
static constexpr float kNewAxeIntegrity = 3.f;
static constexpr float kMinAxeIntegrity = 1.f;
static constexpr int32_t kMinLogsForHouse = 5;

int survivalOnHill( ExampleParameters& params )
{
    gie::World& world = params.world;
    gie::Planner& planner = params.planner;
    gie::Goal& goal = params.goal;

    // specific example visualization
    params.imGuiDrawFunc = &ImGuiFunc5;
    params.glDrawFunc = &GLDrawFunc5;
    params.isGameplayExample = true;

    // creating agent (aka npc)
    auto agentEntity = world.createAgent( "Pawn" );

    // registering agent with tag to be found later in simulation
    world.context().entityTagRegister().tag( agentEntity, { gie::stringHasher( "Agent" ) } );

    // property telling if agent has a wood house
    auto agentWoodHousePpt = agentEntity->createProperty( "WoodHouse", false );

    // Define archetypes used in this example (only once per world)
	if( world.archetypes().empty() )
	{
		// Waypoint
		if( auto* a = world.createArchetype( "Waypoint" ) )
		{
			a->addTag( "Waypoint" );
			a->addTag( "Draw" );
			a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
			a->addProperty( "Links", gie::Property::GuidVector{} );
		}
		// Tree
		if( auto* a = world.createArchetype( "Tree" ) )
		{
			a->addTag( "Tree" );
			a->addTag( "TreeUp" );
			a->addTag( "Draw" );
			a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
		}
	}

    // creating agent properties for this tutorial

    // total money npc carries
    agentEntity->createProperty( "Money", 0.f );
    // things (e.g. axe) npc need to buy
    agentEntity->createProperty( "ThingsToBuy", gie::Property::GuidVector{} );
    // integrity of axe npc is carrying
    agentEntity->createProperty( "AxeIntegrity", 0.f );
    // initial position of agent
    agentEntity->createProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );

    // survival stats (constants defined at file scope above)
    agentEntity->createProperty( "Energy", kMaxEnergy );
    agentEntity->createProperty( "Hunger", 20.f );
    agentEntity->createProperty( "Thirst", 20.f );

    // trees configuration
    constexpr glm::vec2 treeAreaExtent{ 2.f, 2.f };
    constexpr glm::vec3 treeAreaCenter{ 0.f, 20.f, 0.f };
    constexpr int32_t treeMatrixOrder = 3;
    static_assert( treeMatrixOrder > 1 );

    // creating way points which npc can move
    constexpr size_t waypointCount = 14;
    std::array< glm::vec3, waypointCount > waypointLocations
    {
        glm::vec3{   0.f,   0.f,  0.f },
        glm::vec3{  10.f,   5.f,  0.f },
        glm::vec3{  20.f,  10.f,  0.f },
        glm::vec3{  10.f,  15.f,  0.f },
        glm::vec3{   0.f,  20.f,  0.f },
        glm::vec3{ -10.f,  15.f,  0.f },
        glm::vec3{ -20.f,  10.f,  0.f },
        glm::vec3{ -10.f,   5.f,  0.f },
        glm::vec3{  10.f, -10.f,  0.f },
        glm::vec3{ -10.f, -10.f,  0.f },
        glm::vec3{ -15.f,  -5.f,  0.f },
        glm::vec3{ -20.f, -10.f,  0.f },
        glm::vec3{ -15.f, -15.f,  0.f },
        glm::vec3{   0.f,  20.f, 20.f }
    };
    std::array< gie::Property::GuidVector*, waypointCount > waypointLinks;

    std::vector< gie::Entity* > waypoints;
	std::vector< gie::Guid > waypointGuids;
	waypoints.reserve( waypointCount );
	waypointGuids.reserve( waypointCount );
	const auto waypointTag = gie::stringHasher( "Waypoint" );

	// all waypoints and other renderable entities will have this tag
	const auto drawTag = gie::stringHasher( "Draw" );

	for( size_t i = 0; i < waypointCount; i++ )
	{
		auto& waypointLocation = waypointLocations[ i ];
		auto wypointEntityName = std::string( "waypoint" + std::to_string( i ) );
		auto& createdWaypoint = waypoints.emplace_back( world.createEntity( wypointEntityName ) );
		world.context().entityTagRegister().tag( createdWaypoint, { waypointTag, drawTag } );
		waypointGuids.push_back( createdWaypoint->guid() );
		createdWaypoint->createProperty( "Location", waypointLocation );
		auto linksPpt = createdWaypoint->createProperty( "Links", gie::Property::GuidVector{} );
		waypointLinks[ i ] = linksPpt->getGuidArray();
	}

    // connecting waypoints
    constexpr bool linkLadder = false;

    {
        gie::Guid wp0Guid = waypoints[ 0 ]->guid();
        gie::Guid wp1Guid = waypoints[ 1 ]->guid();
        gie::Guid wp2Guid = waypoints[ 2 ]->guid();
        gie::Guid wp3Guid = waypoints[ 3 ]->guid();
        gie::Guid wp4Guid = waypoints[ 4 ]->guid();
        gie::Guid wp5Guid = waypoints[ 5 ]->guid();
        gie::Guid wp6Guid = waypoints[ 6 ]->guid();
        gie::Guid wp7Guid = waypoints[ 7 ]->guid();
        gie::Guid wp8Guid = waypoints[ 8 ]->guid();
        gie::Guid wp9Guid = waypoints[ 9 ]->guid();
        gie::Guid wp10Guid = waypoints[ 10 ]->guid();
        gie::Guid wp11Guid = waypoints[ 11 ]->guid();
        gie::Guid wp12Guid = waypoints[ 12 ]->guid();
        gie::Guid wp13Guid = waypoints[ 13 ]->guid();

        // wp0
        waypointLinks[ 0 ]->push_back( wp1Guid );
        waypointLinks[ 0 ]->push_back( wp7Guid );
        waypointLinks[ 0 ]->push_back( wp8Guid );
        waypointLinks[ 0 ]->push_back( wp9Guid );
        // wp1 (not linked to wp13 initially)
        if( linkLadder )
        {
            waypointLinks[ 1 ]->push_back( wp13Guid );
        }
        waypointLinks[ 1 ]->push_back( wp2Guid );
        waypointLinks[ 1 ]->push_back( wp0Guid );
        waypointLinks[ 1 ]->push_back( wp8Guid );
        // wp2
        waypointLinks[ 2 ]->push_back( wp3Guid );
        waypointLinks[ 2 ]->push_back( wp1Guid );
        // wp3
        waypointLinks[ 3 ]->push_back( wp4Guid );
        waypointLinks[ 3 ]->push_back( wp2Guid );
        // wp4
        waypointLinks[ 4 ]->push_back( wp5Guid );
        waypointLinks[ 4 ]->push_back( wp3Guid );
        waypointLinks[ 4 ]->push_back( wp13Guid );
        // wp5
        waypointLinks[ 5 ]->push_back( wp6Guid );
        waypointLinks[ 5 ]->push_back( wp4Guid );
        // wp6
        waypointLinks[ 6 ]->push_back( wp7Guid );
        waypointLinks[ 6 ]->push_back( wp5Guid );
        // wp7
        waypointLinks[ 7 ]->push_back( wp0Guid );
        waypointLinks[ 7 ]->push_back( wp6Guid );
        // wp8
        waypointLinks[ 8 ]->push_back( wp1Guid );
        waypointLinks[ 8 ]->push_back( wp0Guid );
        waypointLinks[ 8 ]->push_back( wp9Guid );
        // wp9
        waypointLinks[ 9 ]->push_back( wp0Guid );
        waypointLinks[ 9 ]->push_back( wp8Guid );
        waypointLinks[ 9 ]->push_back( wp10Guid );
        waypointLinks[ 9 ]->push_back( wp11Guid );
        waypointLinks[ 9 ]->push_back( wp12Guid );
        // wp10
        waypointLinks[ 10 ]->push_back( wp9Guid );
        // wp11
        waypointLinks[ 11 ]->push_back( wp9Guid );
        // wp12
        waypointLinks[ 12 ]->push_back( wp9Guid );
        // wp13 (not linking it initially)
        if( linkLadder )
        {
            waypointLinks[ 13 ]->push_back( wp1Guid );
        }
        waypointLinks[ 13 ]->push_back( wp4Guid );
    }

    auto pathResult = gie::getPath( world, waypointGuids, glm::vec3{ 0.f, 0.f, 0.f }, glm::vec3{ 0.f, 20.f, 20.f } );
    gie::printPath( waypointGuids, pathResult );

    // creating entity defining axe (a thing) npc can buy
    auto axeInfoEntity = world.createEntity( "AxeInfo" );
    axeInfoEntity->createProperty( "Name", "Axe" );
    axeInfoEntity->createProperty( "Price", kAxePrice );

    // registering entity with tag to be found later in simulation
    world.context().entityTagRegister().tag( axeInfoEntity, { gie::stringHasher( "AxeInfo" ) } );

    // setting goal targets (agent's wood house must exist)
    goal.targets.emplace_back( agentWoodHousePpt->guid(), true );

    // world survival resources
    // create a couple of food sources and water sources at existing waypoints
    auto food0 = world.createEntity( "FoodSource0" );
    food0->createProperty( "Location", waypointLocations[ 8 ] );
    world.context().entityTagRegister().tag( food0, { gie::stringHasher( "FoodSource" ) } );
    auto food1 = world.createEntity( "FoodSource1" );
    food1->createProperty( "Location", waypointLocations[ 2 ] );
    world.context().entityTagRegister().tag( food1, { gie::stringHasher( "FoodSource" ) } );

    auto water0 = world.createEntity( "WaterSource0" );
    water0->createProperty( "Location", waypointLocations[ 11 ] );
    world.context().entityTagRegister().tag( water0, { gie::stringHasher( "WaterSource" ) } );
    auto water1 = world.createEntity( "WaterSource1" );
    water1->createProperty( "Location", waypointLocations[ 4 ] );
    world.context().entityTagRegister().tag( water1, { gie::stringHasher( "WaterSource" ) } );

    // workplace and construction site to enable path-based costs for work/build
    auto workplace = world.createEntity( "Workplace0" );
    workplace->createProperty( "Location", waypointLocations[ 9 ] );
    world.context().entityTagRegister().tag( workplace, { gie::stringHasher( "Workplace" ) } );

    auto construct = world.createEntity( "ConstructionSite" );
    construct->createProperty( "Location", waypointLocations[ 4 ] );
    world.context().entityTagRegister().tag( construct, { gie::stringHasher( "ConstructionSite" ) } );

    // setting tree positions
    constexpr size_t treeCount = 6;
    std::array< glm::vec3, treeCount > treeLocations
    {
        glm::vec3{  -6.f,   0.f,  0.f },
        glm::vec3{  12.f,  -3.f,  0.f },
        glm::vec3{  25.f,   2.f,  0.f },
        glm::vec3{   8.f,  -4.f,  0.f },
        glm::vec3{   0.f,  -3.f,  0.f },
        glm::vec3{ -12.f,  -4.f,  0.f }
    };

    const auto treeTag = gie::stringHasher( "Tree" );
	const auto treeUpTag = gie::stringHasher( "TreeUp" );

	// adding trees to world
	for( size_t i = 0; i < treeCount; i++ )
	{
		auto treeEntity = world.createEntity( std::string( "tree" + std::to_string( i ) ) );
		treeEntity->createProperty( "Location", treeLocations[ i ] );
		world.context().entityTagRegister().tag( treeEntity, { treeTag, treeUpTag, drawTag } );
	}

    // setting up planner passing goal and agent to reach the goal
    planner.simulate( goal, *agentEntity );

    RegisterActions( planner );

    // Run gameplay loop (deferred in visualization mode — triggered by GUI button)
    if( !params.visualize )
    {
        RunGameplayLoop( params.world, agentEntity, params.planner );
    }

    return 0;
}

// ---------------------------------------------------------------------------
// RegisterActions — all 7 planner action registrations
// ---------------------------------------------------------------------------
static void RegisterActions( gie::Planner& planner )
{
    // defining available actions via lambda-based API

    // -----------------------------------------------------------------------
    // CutDownTree: cut down nearest tree if agent has axe integrity
    // -----------------------------------------------------------------------
    planner.addLambdaAction( "CutDownTree",
        // evaluate
        [=]( gie::EvaluateSimulationParams params ) -> bool
        {
            auto& context            = params.simulation.context();
            auto agentEntity        = context.entity( params.agent.guid() );
            auto axeIntegrityPpt    = agentEntity->property( "AxeIntegrity" );

            // survival constraints
            auto energyP = agentEntity->property( "Energy" );
            auto hungerP = agentEntity->property( "Hunger" );
            auto thirstP = agentEntity->property( "Thirst" );

            params.addDebugMessage( "CutDownTreeSimulator::evaluate" );

            // If too hungry or thirsty, defer to Eat/Drink
            if( hungerP && *hungerP->getFloat() >= kHungerHigh )
            {
                params.addDebugMessage( "Too hungry to cut trees" );
                return false;
            }
            if( thirstP && *thirstP->getFloat() >= kThirstHigh )
            {
                params.addDebugMessage( "Too thirsty to cut trees" );
                return false;
            }
            if( energyP && *energyP->getFloat() < kMinEnergyToAct )
            {
                params.addDebugMessage( "Not enough energy to cut trees" );
                return false;
            }

            // checking minimal axe integrity to cut down a tree
            if( *axeIntegrityPpt->getFloat() < kMinAxeIntegrity )
            {
                params.addDebugMessage( "No integrity, returning FALSE" );

                // adding argument about needing axe to cut down tree

                auto axeInfoTagSet = params.simulation.context().entityTagRegister().tagSet( "AxeInfo" );
                auto axeInfoEntityGuid = *axeInfoTagSet->cbegin();
                auto thingsToBuyArgument = params.arguments().get( "ThingsToBuy" );
                if( !thingsToBuyArgument )
                {
                    params.addDebugMessage( "No thingsToBuy argument, creating one and adding axe info entity guid to it" );
                    params.arguments().add( "ThingsToBuy", gie::Property::GuidVector{ axeInfoEntityGuid } );
                }
                else
                {
                    params.addDebugMessage( "Adding axe info entity guid to ThingsToBuy argument" );
                    auto& thingsToBuyArray = std::get< gie::Property::GuidVector >( *thingsToBuyArgument );
                    if( std::find( thingsToBuyArray.cbegin(), thingsToBuyArray.cend(), axeInfoEntityGuid ) == thingsToBuyArray.cend() )
                    {
                        thingsToBuyArray.emplace_back( axeInfoEntityGuid );
                    }
                }

                return false;
            }

            // getting set of trees still up
            const auto treeUpTagSet = context.entityTagRegister().tagSet( "TreeUp" );

            // no tree up found
            if( treeUpTagSet->empty() )
            {
                params.addDebugMessage( "No trees up, returning FALSE" );
                return false;
            }

            params.addDebugMessage( "Has axe integrity and trees up, returning TRUE" );
            return true;
        },
        // simulate
        [=]( gie::SimulateSimulationParams params ) -> bool
        {
            auto& context                = params.simulation.context();
            auto agentEntity            = context.entity( params.agent.guid() );
            auto axeIntegrityPpt        = agentEntity->property( "AxeIntegrity" );

            // getting set of trees still up
            const auto treeUpTagSet        = params.simulation.tagSet( "TreeUp" );

            // choose nearest tree by path length using waypoint graph
            gie::Guid chosenTreeGuid = gie::NullGuid;
            float bestLength = std::numeric_limits< float >::max();
            glm::vec3* agentLocation = agentEntity->property( "Location" )->getVec3();
            // collect waypoints
            auto waypointTagSet = params.simulation.tagSet( "Waypoint" );
            std::vector< gie::Guid > waypointGuids;
            if( waypointTagSet )
            {
                waypointGuids.assign( waypointTagSet->begin(), waypointTagSet->end() );
            }
            gie::PathfindingResult pathResult;
            gie::PathfindingSteps pathSolverSteps; // capture steps for the chosen path

            for( auto treeGuid : *treeUpTagSet )
            {
                if( auto treeEntity = context.entity( treeGuid ) )
                {
                    auto locPpt = treeEntity->property( "Location" );
                    if( locPpt )
                    {
                        glm::vec3 treeLoc = *locPpt->getVec3();
                        if( !waypointGuids.empty() )
                        {
                            gie::PathfindingSteps steps; // temporary collector
                            auto candidate = gie::getPath( *params.simulation.world(), waypointGuids, *agentLocation, treeLoc, &steps );
                            if( candidate.length < bestLength )
                            {
                                bestLength = candidate.length;
                                chosenTreeGuid = treeGuid;
                                pathResult = std::move( candidate );
                                pathSolverSteps = std::move( steps );
                            }
                        }
                    }
                }
            }

            // fallback to first available if path or waypoints are missing
            if( chosenTreeGuid == gie::NullGuid )
            {
                chosenTreeGuid = *treeUpTagSet->cbegin();
            }

            // storing path to chosen tree for visualization
            if( !pathResult.path.empty() )
            {
                gie::storeSimulatedPath( params, pathResult, chosenTreeGuid, agentLocation );
            }

            // store path solving steps used to reach chosen tree so visualization can render it
            auto chosenTreeEntity = context.entity( chosenTreeGuid );
            gie::Property* chosenTreeLocPpt = chosenTreeEntity ? chosenTreeEntity->property( "Location" ) : nullptr;
            if( !pathSolverSteps.states.empty() && chosenTreeLocPpt )
            {
                glm::vec3 chosenTreeLoc = *chosenTreeLocPpt->getVec3();
                gie::storeSimulatedPathFindingSteps( pathSolverSteps, params );
                *agentLocation = chosenTreeLoc;
            }

            // action tool usage: reduce axe integrity
            axeIntegrityPpt->value = *axeIntegrityPpt->getFloat() - 1.f;

            // apply travel and base deltas after we know path length
            float pathLen = pathResult.length;
            ApplyTravelAndBaseDeltas( agentEntity, /*baseEnergy*/ 8.f, /*baseHunger*/ 5.f, /*baseThirst*/ 7.f, pathLen, kEnergyPerPath, kHungerPerPath, kThirstPerPath, kMaxEnergy, kMaxHunger, kMaxThirst, params, "CutDownTree" );

            // consuming chosen tree
            auto treeEntity                = context.entity( chosenTreeGuid );
            auto& entityTagRegister        = context.entityTagRegister();

            params.addDebugMessage( "CutDownTreeSimulator::simulate" );
            const std::string treeName{ gie::stringRegister().get( treeEntity->nameHash() ) };
            params.addDebugMessage( "Cutting down tree: " + treeName );
            params.addDebugMessage( "Changing TreeUp tag to TreeDown: " + treeName );

            entityTagRegister.untag( treeEntity, { gie::stringHasher( "TreeUp" ) } );
            entityTagRegister.tag( treeEntity, { gie::stringHasher( "TreeDown" ) } );

            // creating cut down tree action with target argument
            auto cutDownTreeAction = std::make_shared< gie::Action >( gie::stringHasher( "CutDownTree" ) );
            cutDownTreeAction->arguments().add( { gie::stringHasher( "TargetTree" ), chosenTreeGuid } );
            params.simulation.actions.emplace_back( cutDownTreeAction );
            params.addDebugMessage( "CutDownTreeAction added, returning TRUE" );
            return true;
        },
        // heuristic: path length from agent to nearest tree
        []( gie::CalculateHeuristicParams params ) -> void
        {
            auto& context = params.simulation.context();
            auto agentEntity = context.entity( params.agent.guid() );
            if( !agentEntity ) return;
            glm::vec3 agentLocation = *agentEntity->property( "Location" )->getVec3();
            const auto treeUpTagSet = params.simulation.tagSet( "TreeUp" );
            if( !treeUpTagSet || treeUpTagSet->empty() )
            {
                params.simulation.heuristic.value = 0.f;
                return;
            }
            // collect waypoints
            auto waypointTagSet = params.simulation.tagSet( "Waypoint" );
            if( !waypointTagSet || waypointTagSet->empty() )
            {
                params.simulation.heuristic.value = 0.f;
                return;
            }
            std::vector< gie::Guid > waypointGuids{ waypointTagSet->begin(), waypointTagSet->end() };
            float bestLength = std::numeric_limits< float >::max();
            for( auto treeGuid : *treeUpTagSet )
            {
                if( auto treeEntity = context.entity( treeGuid ) )
                {
                    if( auto locPpt = treeEntity->property( "Location" ) )
                    {
                        glm::vec3 treeLoc = *locPpt->getVec3();
                        auto path = gie::getPath( *params.simulation.world(), waypointGuids, agentLocation, treeLoc );
                        bestLength = std::min( bestLength, path.length );
                    }
                }
            }
            if( bestLength == std::numeric_limits< float >::max() )
            {
                bestLength = 0.f;
            }
            params.simulation.heuristic.value = bestLength;
        }
    );

    // -----------------------------------------------------------------------
    // BuildHouse: build a wood house if enough logs are available
    // -----------------------------------------------------------------------
    planner.addLambdaAction( "BuildHouse",
        // evaluate
        [=]( gie::EvaluateSimulationParams params ) -> bool
        {
            auto& context = params.simulation.context();
            auto agentEntity = context.entity( params.agent.guid() );
            auto woodHousePpt = agentEntity->property( "WoodHouse" );

            params.addDebugMessage( "BuildHouseSimulator::evaluate" );

            // survival constraints
            if( auto hunger = agentEntity->property( "Hunger" ) )
            {
                if( *hunger->getFloat() >= kHungerHigh ) return false;
            }
            if( auto thirst = agentEntity->property( "Thirst" ) )
            {
                if( *thirst->getFloat() >= kThirstHigh ) return false;
            }
            if( auto energy = agentEntity->property( "Energy" ) )
            {
                if( *energy->getFloat() < kMinEnergyToAct ) return false;
            }

            // if agent already has a wood house, no need to build another one
            if( *woodHousePpt->getBool() )
            {
                params.addDebugMessage( "Agent already has a wood house, returning FALSE" );
                return false;
            }

            // getting set of trees that have been cut down (logs available)
            const auto treeDownTagSet = context.entityTagRegister().tagSet( "TreeDown" );

            // count available logs
            const int32_t availableLogs = static_cast<int32_t>( treeDownTagSet ? treeDownTagSet->size() : 0 );

            params.addDebugMessage( "Available logs: " + std::to_string( availableLogs ) );
            params.addDebugMessage( "Required logs: " + std::to_string( kMinLogsForHouse ) );

            // check if we have enough logs to build a house
            if( availableLogs >= kMinLogsForHouse )
            {
                params.addDebugMessage( "Enough logs available to build house, returning TRUE" );
                return true;
            }

            params.addDebugMessage( "Not enough logs to build house, returning FALSE" );
            return false;
        },
        // simulate
        [=]( gie::SimulateSimulationParams params ) -> bool
        {
            auto& context = params.simulation.context();
            auto agentEntity = context.entity( params.agent.guid() );
            auto woodHousePpt = agentEntity->property( "WoodHouse" );

            params.addDebugMessage( "BuildHouseSimulator::simulate" );

            // path to construction site for travel-based costs
            auto siteSet = params.simulation.tagSet( "ConstructionSite" );
            float pathLen = 0.f;
            if( siteSet && !siteSet->empty() )
            {
                auto waypointTagSet = params.simulation.tagSet( "Waypoint" );
                std::vector< gie::Guid > waypointGuids;
                if( waypointTagSet ) waypointGuids.assign( waypointTagSet->begin(), waypointTagSet->end() );
                glm::vec3* agentLoc = agentEntity->property( "Location" )->getVec3();
                float best = std::numeric_limits<float>::max();
                gie::Guid bestSite = gie::NullGuid;
                gie::PathfindingResult bestPath;
                for( auto g : *siteSet )
                {
                    if( auto e = context.entity( g ) )
                    {
                        if( auto lp = e->property( "Location" ) )
                        {
                            auto target = *lp->getVec3();
                            auto candidate = gie::getPath( *params.simulation.world(), waypointGuids, *agentLoc, target );
                            if( candidate.length < best )
                            {
                                best = candidate.length;
                                bestSite = g;
                                bestPath = std::move( candidate );
                            }
                        }
                    }
                }
                if( bestSite != gie::NullGuid )
                {
                    pathLen = bestPath.length;
                    gie::storeSimulatedPath( params, bestPath, bestSite, agentLoc );
                    if( auto e = context.entity( bestSite ) )
                    {
                        if( auto lp = e->property( "Location" ) )
                        {
                            *agentLoc = *lp->getVec3();
                        }
                    }
                }
            }

            // getting set of trees that have been cut down
            const auto treeDownTagSet = params.simulation.tagSet( "TreeDown" );

            // count available logs
            const int32_t availableLogs = static_cast<int32_t>( treeDownTagSet ? treeDownTagSet->size() : 0 );

            if( availableLogs >= kMinLogsForHouse )
            {
                // apply travel/base deltas
                ApplyTravelAndBaseDeltas( agentEntity, /*baseEnergy*/ 12.f, /*baseHunger*/ 8.f, /*baseThirst*/ 8.f, pathLen, kEnergyPerPath, kHungerPerPath, kThirstPerPath, kMaxEnergy, kMaxHunger, kMaxThirst, params, "BuildHouse" );

                // build the house!
                woodHousePpt->value = true;

                // consume the required logs by removing TreeDown tags from trees
                // and adding TreeUsed tags to mark them as consumed
                auto& entityTagRegister = context.entityTagRegister();
                int32_t logsConsumed = 0;

                for( auto treeGuid : *treeDownTagSet )
                {
                    if( logsConsumed >= kMinLogsForHouse )
                    {
                        break;
                    }

                    auto treeEntity = context.entity( treeGuid );
                    if( treeEntity )
                    {
                        params.addDebugMessage( "Consuming log from tree" );
                        entityTagRegister.untag( treeEntity, { gie::stringHasher( "TreeDown" ) } );
                        entityTagRegister.tag( treeEntity, { gie::stringHasher( "TreeUsed" ) } );
                        logsConsumed++;
                    }
                }

                // planner auto-pushes the action for simple cases
                params.addDebugMessage( "BuildHouseAction added, house built successfully! Returning TRUE" );
                return true;
            }

            params.addDebugMessage( "BuildHouseAction not added, returning FALSE" );
            return false;
        }
    );

    // -----------------------------------------------------------------------
    // BuyThing: buy an item (axe) if agent has enough money
    // -----------------------------------------------------------------------
    planner.addLambdaAction( "BuyThing",
        // evaluate
        [=]( gie::EvaluateSimulationParams params ) -> bool
        {
            auto& context            = params.simulation.context();
            auto agentEntity        = context.entity( params.agent.guid() );
            auto thingsToBuyPpt        = agentEntity->property( "ThingsToBuy" );
            auto thingsToBuyPptGuids = thingsToBuyPpt->getGuidArray();

            auto thingsToBuyArgument = params.arguments().get( "ThingsToBuy" );
            std::vector< gie::Guid > thingsToBuyGuids = *thingsToBuyPptGuids;

            // summing up things to buy from arguments
            if( thingsToBuyArgument )
            {
                auto& thingsToBuyArgumentGuids = std::get< gie::Property::GuidVector >( *thingsToBuyArgument );
                for( gie::Guid thingToBuyGuid : thingsToBuyArgumentGuids )
                {
                    if( std::find( thingsToBuyGuids.cbegin(), thingsToBuyGuids.cend(), thingToBuyGuid )
                        == thingsToBuyGuids.cend() )
                    {
                        // adding guid from argument if not already in things to buy
                        thingsToBuyGuids.emplace_back( thingToBuyGuid );
                    }
                }
            }

            auto axeInfoTagSet        = context.entityTagRegister().tagSet( "AxeInfo" );
            auto axeInfoEntityGuid    = *axeInfoTagSet->cbegin();

            params.addDebugMessage( "BuyThingSimulator::evaluate" );

            if( std::find( thingsToBuyGuids.begin(), thingsToBuyGuids.end(), axeInfoEntityGuid ) != thingsToBuyGuids.end() )
            {
                // axe is already in things to buy list, so we need to check if there is enough money
                auto moneyPpt = agentEntity->property( "Money" );
                params.addDebugMessage( "Axe is in things to buy, checking money" );
                const float money = *moneyPpt->getFloat();
                const bool hasEnoughMoney = money >= kAxePrice;
                params.addDebugMessage(
                    "Money: " + std::to_string( money ) + ", axe price: " + std::to_string( kAxePrice )
                    + ", has enough money: " + ( hasEnoughMoney ? "TRUE" : "FALSE" ) );
                return hasEnoughMoney;
            }

            params.addDebugMessage( "Axe is not in things to buy, returning FALSE" );
            return false;
        },
        // simulate
        [=]( gie::SimulateSimulationParams params ) -> bool
        {
            auto& context = params.simulation.context();
            auto agentEntity = context.entity( params.agent.guid() );
            auto thingsToBuyPpt = agentEntity->property( "ThingsToBuy" );
            auto axeInfoTagSet = context.entityTagRegister().tagSet( "AxeInfo" );
            auto axeInfoEntityGuid = *axeInfoTagSet->cbegin();
            auto moneyPpt = agentEntity->property( "Money" );

            params.addDebugMessage( "BuyThingSimulator::simulate" );

            // buy axe if there is enough money
            if( *moneyPpt->getFloat() >= kAxePrice )
            {
                auto axeIntegrityPpt = agentEntity->property( "AxeIntegrity" );
                axeIntegrityPpt->value = kNewAxeIntegrity;

                // consuming money
                moneyPpt->value = *moneyPpt->getFloat() - kAxePrice;

                // removing axe from things to buy property
                auto thingsToBuyArray = thingsToBuyPpt->getGuidArray();
                auto newArrayEnd = std::remove( thingsToBuyArray->begin(), thingsToBuyArray->end(), axeInfoEntityGuid );

                thingsToBuyArray->erase( newArrayEnd, thingsToBuyArray->end() );

                // apply a small baseline fatigue for the purchase logistics (no travel path here)
                float zeroPath = 0.f;
                ApplyTravelAndBaseDeltas( agentEntity, /*baseEnergy*/ 2.f, /*baseHunger*/ 1.f, /*baseThirst*/ 1.f, zeroPath, kEnergyPerPath, kHungerPerPath, kThirstPerPath, kMaxEnergy, kMaxHunger, kMaxThirst, params, "BuyThing" );

                // planner auto-pushes the action for simple cases
                params.addDebugMessage( "BuyThingAction added, returning TRUE" );
                return true;
            }
            else
            {
                // no enough money to buy an axe, lets add axe to the
                // "things to buy" list so agent has to look for money
                // to afford it.

                auto thingsToBuyArray = thingsToBuyPpt->getGuidArray();

                if( std::find( thingsToBuyArray->begin(), thingsToBuyArray->end(), axeInfoEntityGuid )
                    != thingsToBuyArray->end() )
                {
                    // it's already set to buy axe
                    return false;
                }

                thingsToBuyArray->emplace_back( axeInfoEntityGuid );

                // setting up action to be used outside the simulation
                gie::NamedArguments actionArguments{};
                actionArguments.add( { gie::stringHasher( "ThingToBuy" ), axeInfoEntityGuid } );

                auto raiseMoneyNeededAction = std::make_shared< gie::Action >( gie::stringHasher( "NewThingToBuy" ), actionArguments );
                params.simulation.actions.emplace_back( raiseMoneyNeededAction );
                params.addDebugMessage( "NewThingToBuyAction added, returning TRUE" );
                return true;
            }

            return false;
        }
    );

    // -----------------------------------------------------------------------
    // Work: earn money to afford things to buy
    // -----------------------------------------------------------------------
    planner.addLambdaAction( "Work",
        // evaluate
        [=]( gie::EvaluateSimulationParams params ) -> bool
        {
            auto& context                = params.simulation.context();
            auto agentEntity            = context.entity( params.agent.guid() );
            auto moneyPpt                = agentEntity->property( "Money" );
            auto thingsToBuyPpt            = agentEntity->property( "ThingsToBuy" );
            auto thingsToBuyPptGuids    = thingsToBuyPpt->getGuidArray();
            auto thingsToBuyArgument    = params.arguments().get( "ThingsToBuy" );

            // survival constraints
            if( auto hunger = agentEntity->property( "Hunger" ) )
            {
                if( *hunger->getFloat() >= kHungerHigh ) return false;
            }
            if( auto thirst = agentEntity->property( "Thirst" ) )
            {
                if( *thirst->getFloat() >= kThirstHigh ) return false;
            }
            if( auto energy = agentEntity->property( "Energy" ) )
            {
                if( *energy->getFloat() < kMinEnergyToAct ) return false;
            }

            std::vector< gie::Guid > thingsToBuyGuids = *thingsToBuyPptGuids;

            // summing up things to buy from arguments
            if( thingsToBuyArgument )
            {
                auto& thingsToBuyArgumentGuids = std::get< gie::Property::GuidVector >( *thingsToBuyArgument );
                for( gie::Guid thingToBuyGuid : thingsToBuyArgumentGuids )
                {
                    if( std::find( thingsToBuyGuids.cbegin(), thingsToBuyGuids.cend(), thingToBuyGuid ) == thingsToBuyGuids.cend() )
                    {
                        // adding guid from argument if not already in things to buy
                        thingsToBuyGuids.emplace_back( thingToBuyGuid );
                    }
                }
            }

            // getting cost of things to buy
            float cost = 0.f;
            for( gie::Guid thingToBuyGuid : thingsToBuyGuids )
            {
                if( auto thingToBuyEntity = context.entity( thingToBuyGuid ) )
                {
                    auto thingPricePpt = thingToBuyEntity->property( "Price" );
                    if( thingPricePpt )
                    {
                        cost += *thingPricePpt->getFloat();
                    }
                }
            }

            params.addDebugMessage( "WorkSimulator::evaluate" );
            params.addDebugMessage( "Cost of things to buy is: " + std::to_string( cost ) );
            params.addDebugMessage( "Money: " + std::to_string( *moneyPpt->getFloat() ) );

            // no need to work if have enough money to buy stuff
            if( *moneyPpt->getFloat() >= cost )
            {
                params.addDebugMessage( "Have enough money, no need to work, returning FALSE" );
                return false;
            }

            params.addDebugMessage( "Not enough money, need to work, returning TRUE" );
            return true;
        },
        // simulate (calculate cost and necessary steps to achieve the action)
        [=]( gie::SimulateSimulationParams params ) -> bool
        {
            auto& context        = params.simulation.context();
            auto agentEntity    = context.entity( params.agent.guid() );

            // path to workplace to account travel costs
            auto workSet = params.simulation.tagSet( "Workplace" );
            float pathLen = 0.f;
            if( workSet && !workSet->empty() )
            {
                auto waypointTagSet = params.simulation.tagSet( "Waypoint" );
                std::vector< gie::Guid > waypointGuids;
                if( waypointTagSet ) waypointGuids.assign( waypointTagSet->begin(), waypointTagSet->end() );
                glm::vec3* agentLoc = agentEntity->property( "Location" )->getVec3();
                float best = std::numeric_limits<float>::max();
                gie::Guid bestWork = gie::NullGuid;
                gie::PathfindingResult bestPath;
                for( auto g : *workSet )
                {
                    if( auto e = context.entity( g ) )
                    {
                        if( auto lp = e->property( "Location" ) )
                        {
                            auto target = *lp->getVec3();
                            auto candidate = gie::getPath( *params.simulation.world(), waypointGuids, *agentLoc, target );
                            if( candidate.length < best )
                            {
                                best = candidate.length;
                                bestWork = g;
                                bestPath = std::move( candidate );
                            }
                        }
                    }
                }
                if( bestWork != gie::NullGuid )
                {
                    pathLen = bestPath.length;
                    gie::storeSimulatedPath( params, bestPath, bestWork, agentLoc );
                    if( auto e = context.entity( bestWork ) )
                    {
                        if( auto lp = e->property( "Location" ) )
                        {
                            *agentLoc = *lp->getVec3();
                        }
                    }
                }
            }

            // adding up money
            auto moneyPpt = agentEntity->property( "Money" );
            moneyPpt->value = *moneyPpt->getFloat() + kWorkSalary;

            // apply travel and base deltas
            ApplyTravelAndBaseDeltas( agentEntity, /*baseEnergy*/ 6.f, /*baseHunger*/ 5.f, /*baseThirst*/ 6.f, pathLen, kEnergyPerPath, kHungerPerPath, kThirstPerPath, kMaxEnergy, kMaxHunger, kMaxThirst, params, "Work" );

            params.addDebugMessage( "WorkSimulator::simulate" );
            params.addDebugMessage( "Working, adding " + std::to_string( kWorkSalary ) + " to money" );

            // planner auto-pushes the action for simple cases
            params.addDebugMessage( "WorkAction added, returning TRUE" );
            return true;
        }
    );

    // -----------------------------------------------------------------------
    // EatFood: eat at nearest food source when hunger is high
    // -----------------------------------------------------------------------
    planner.addLambdaAction( "EatFood",
        // evaluate
        [=]( gie::EvaluateSimulationParams params ) -> bool
        {
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            auto hunger = agent->property( "Hunger" );
            params.addDebugMessage( "EatFoodSimulator::evaluate" );
            if( !hunger ) { params.addDebugMessage( "No Hunger property, returning FALSE" ); return false; }
            params.addDebugMessage( "Hunger: " + std::to_string( *hunger->getFloat() ) );
            if( *hunger->getFloat() < kHungerHigh ) { params.addDebugMessage( "Hunger below threshold, returning FALSE" ); return false; }
            auto foodSet = ctx.entityTagRegister().tagSet( "FoodSource" );
            const bool ok = foodSet && !foodSet->empty();
            params.addDebugMessage( std::string( "Food sources available: " ) + ( ok ? "YES" : "NO" ) );
            return ok;
        },
        // simulate
        [=]( gie::SimulateSimulationParams params ) -> bool
        {
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            params.addDebugMessage( "EatFoodSimulator::simulate" );
            auto foodSet = params.simulation.tagSet( "FoodSource" );
            if( !foodSet || foodSet->empty() ) { params.addDebugMessage( "No FoodSource, returning FALSE" ); return false; }

            // pick closest food by path
            auto waypointTagSet = params.simulation.tagSet( "Waypoint" );
            std::vector< gie::Guid > waypointGuids;
            if( waypointTagSet ) waypointGuids.assign( waypointTagSet->begin(), waypointTagSet->end() );
            glm::vec3* agentLoc = agent->property( "Location" )->getVec3();
            float best = std::numeric_limits<float>::max();
            gie::Guid bestFood = gie::NullGuid;
            gie::PathfindingResult bestPath;
            for( auto g : *foodSet )
            {
                if( auto e = ctx.entity( g ) )
                {
                    if( auto lp = e->property( "Location" ) )
                    {
                        auto target = *lp->getVec3();
                        auto candidate = gie::getPath( *params.simulation.world(), waypointGuids, *agentLoc, target );
                        if( candidate.length < best )
                        {
                            best = candidate.length;
                            bestFood = g;
                            bestPath = std::move( candidate );
                        }
                    }
                }
            }
            if( bestFood == gie::NullGuid ) bestFood = *foodSet->cbegin();

            float pathLen = 0.f;
            if( !bestPath.path.empty() )
            {
                pathLen = bestPath.length;
                params.addDebugMessage( "Storing path to FoodSource, length: " + std::to_string( bestPath.length ) );
                gie::storeSimulatedPath( params, bestPath, bestFood, agentLoc );
                if( auto e = ctx.entity( bestFood ) )
                {
                    if( auto lp = e->property( "Location" ) )
                    {
                        *agentLoc = *lp->getVec3();
                    }
                }
            }

            // apply travel/base deltas for going to food
            ApplyTravelAndBaseDeltas( agent, /*baseEnergy*/ 3.f, /*baseHunger*/ 2.f, /*baseThirst*/ 2.f, pathLen, kEnergyPerPath, kHungerPerPath, kThirstPerPath, kMaxEnergy, kMaxHunger, kMaxThirst, params, "EatFood(travel)" );

            // apply eating effect afterwards
            const float hungerBefore = *agent->property( "Hunger" )->getFloat();
            if( auto hunger = agent->property( "Hunger" ) )
            {
                hunger->value = clampf( *hunger->getFloat() - 60.f, 0.f, kMaxHunger );
            }
            params.addDebugMessage( "EatFood effect -> Hunger: " + std::to_string( hungerBefore ) + " -> " + std::to_string( *agent->property( "Hunger" )->getFloat() ) );

            // push action with target argument
            auto eatAction = std::make_shared< gie::Action >( gie::stringHasher( "EatFood" ) );
            eatAction->arguments().add( { gie::stringHasher( "Target" ), bestFood } );
            params.simulation.actions.emplace_back( eatAction );
            params.addDebugMessage( "EatFoodAction added, returning TRUE" );
            return true;
        }
    );

    // -----------------------------------------------------------------------
    // DrinkWater: drink at nearest water source when thirst is high
    // -----------------------------------------------------------------------
    planner.addLambdaAction( "DrinkWater",
        // evaluate
        [=]( gie::EvaluateSimulationParams params ) -> bool
        {
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            auto thirst = agent->property( "Thirst" );
            params.addDebugMessage( "DrinkWaterSimulator::evaluate" );
            if( !thirst ) { params.addDebugMessage( "No Thirst property, returning FALSE" ); return false; }
            params.addDebugMessage( "Thirst: " + std::to_string( *thirst->getFloat() ) );
            if( *thirst->getFloat() < kThirstHigh ) { params.addDebugMessage( "Thirst below threshold, returning FALSE" ); return false; }
            auto waterSet = ctx.entityTagRegister().tagSet( "WaterSource" );
            const bool ok = waterSet && !waterSet->empty();
            params.addDebugMessage( std::string( "Water sources available: " ) + ( ok ? "YES" : "NO" ) );
            return ok;
        },
        // simulate
        [=]( gie::SimulateSimulationParams params ) -> bool
        {
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            params.addDebugMessage( "DrinkWaterSimulator::simulate" );
            auto waterSet = params.simulation.tagSet( "WaterSource" );
            if( !waterSet || waterSet->empty() ) { params.addDebugMessage( "No WaterSource, returning FALSE" ); return false; }

            // pick closest water by path
            auto waypointTagSet = params.simulation.tagSet( "Waypoint" );
            std::vector< gie::Guid > waypointGuids;
            if( waypointTagSet ) waypointGuids.assign( waypointTagSet->begin(), waypointTagSet->end() );
            glm::vec3* agentLoc = agent->property( "Location" )->getVec3();
            float best = std::numeric_limits<float>::max();
            gie::Guid bestWater = gie::NullGuid;
            gie::PathfindingResult bestPath;
            for( auto g : *waterSet )
            {
                if( auto e = ctx.entity( g ) )
                {
                    if( auto lp = e->property( "Location" ) )
                    {
                        auto target = *lp->getVec3();
                        auto candidate = gie::getPath( *params.simulation.world(), waypointGuids, *agentLoc, target );
                        if( candidate.length < best )
                        {
                            best = candidate.length;
                            bestWater = g;
                            bestPath = std::move( candidate );
                        }
                    }
                }
            }
            if( bestWater == gie::NullGuid ) bestWater = *waterSet->cbegin();

            float pathLen = 0.f;
            if( !bestPath.path.empty() )
            {
                pathLen = bestPath.length;
                params.addDebugMessage( "Storing path to WaterSource, length: " + std::to_string( bestPath.length ) );
                gie::storeSimulatedPath( params, bestPath, bestWater, agentLoc );
                if( auto e = ctx.entity( bestWater ) )
                {
                    if( auto lp = e->property( "Location" ) )
                    {
                        *agentLoc = *lp->getVec3();
                    }
                }
            }

            // apply travel/base deltas for going to water
            ApplyTravelAndBaseDeltas( agent, /*baseEnergy*/ 2.f, /*baseHunger*/ 2.f, /*baseThirst*/ 3.f, pathLen, kEnergyPerPath, kHungerPerPath, kThirstPerPath, kMaxEnergy, kMaxHunger, kMaxThirst, params, "DrinkWater(travel)" );

            // apply drinking effect afterwards
            const float thirstBefore = *agent->property( "Thirst" )->getFloat();
            if( auto thirst = agent->property( "Thirst" ) )
            {
                thirst->value = clampf( *thirst->getFloat() - 70.f, 0.f, kMaxThirst );
            }
            params.addDebugMessage( "DrinkWater effect -> Thirst: " + std::to_string( thirstBefore ) + " -> " + std::to_string( *agent->property( "Thirst" )->getFloat() ) );

            // push action with target argument
            auto drinkAction = std::make_shared< gie::Action >( gie::stringHasher( "DrinkWater" ) );
            drinkAction->arguments().add( { gie::stringHasher( "Target" ), bestWater } );
            params.simulation.actions.emplace_back( drinkAction );
            params.addDebugMessage( "DrinkWaterAction added, returning TRUE" );
            return true;
        }
    );

    // -----------------------------------------------------------------------
    // Sleep: rest to restore energy (increases hunger/thirst slightly)
    // -----------------------------------------------------------------------
    planner.addLambdaAction( "Sleep",
        // evaluate
        []( gie::EvaluateSimulationParams params ) -> bool
        {
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            auto energy = agent->property( "Energy" );
            params.addDebugMessage( "SleepSimulator::evaluate" );
            if( !energy ) { params.addDebugMessage( "No Energy property, returning FALSE" ); return false; }
            params.addDebugMessage( "Energy: " + std::to_string( *energy->getFloat() ) );
            const bool ok = *energy->getFloat() <= 60.f;
            params.addDebugMessage( std::string( "Should sleep: " ) + ( ok ? "YES" : "NO" ) );
            return ok;
        },
        // simulate
        []( gie::SimulateSimulationParams params ) -> bool
        {
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            params.addDebugMessage( "SleepSimulator::simulate" );

            // No travel for sleeping (uses current location) but still increases hunger and thirst slightly over time
            float pathLen = 0.f;
            ApplyTravelAndBaseDeltas( agent, /*baseEnergy*/ -60.f, /*baseHunger*/ 5.f, /*baseThirst*/ 5.f, pathLen, kEnergyPerPath, kHungerPerPath, kThirstPerPath, kMaxEnergy, kMaxHunger, kMaxThirst, params, "Sleep(rest)" );

            // planner auto-pushes the action for simple cases
            params.addDebugMessage( "SleepAction added, returning TRUE" );
            return true;
        }
    );
}

// ---------------------------------------------------------------------------
// Execute functions — mutate the real world during gameplay
// ---------------------------------------------------------------------------
static std::string ExecuteCutDownTree( gie::World& world, gie::Agent* agent )
{
    auto integrityPpt = agent->property( "AxeIntegrity" );
    integrityPpt->value = *integrityPpt->getFloat() - 1.f;

    auto treeUpSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "TreeUp" ) );
    if( !treeUpSet || treeUpSet->empty() ) return "no trees available";

    // Find nearest tree by path
    glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();
    auto wpSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "Waypoint" ) );
    std::vector<gie::Guid> waypointGuids;
    if( wpSet ) waypointGuids.assign( wpSet->begin(), wpSet->end() );

    gie::Guid bestTree = gie::NullGuid;
    float bestLen = std::numeric_limits<float>::max();
    gie::PathfindingResult bestPath;
    for( auto treeGuid : *treeUpSet )
    {
        auto tree = world.entity( treeGuid );
        if( !tree ) continue;
        auto loc = tree->property( "Location" );
        if( !loc ) continue;
        auto candidate = gie::getPath( world, waypointGuids, agentLoc, *loc->getVec3() );
        if( candidate.length < bestLen )
        {
            bestLen = candidate.length;
            bestTree = treeGuid;
            bestPath = std::move( candidate );
        }
    }
    if( bestTree == gie::NullGuid ) bestTree = *treeUpSet->cbegin();

    // Record path
    g_LastActionPath.clear();
    g_LastActionPath.push_back( agentLoc );
    for( auto& wpGuid : bestPath.path )
    {
        auto wp = world.entity( wpGuid );
        if( wp )
        {
            auto loc = wp->property( "Location" );
            if( loc ) g_LastActionPath.push_back( *loc->getVec3() );
        }
    }

    // Move agent to tree
    auto treeEntity = world.entity( bestTree );
    if( treeEntity )
    {
        auto loc = treeEntity->property( "Location" );
        if( loc )
        {
            g_LastActionPath.push_back( *loc->getVec3() );
            *agent->property( "Location" )->getVec3() = *loc->getVec3();
        }
    }

    // Cut tree
    world.context().entityTagRegister().untag( treeEntity, { gie::stringHasher( "TreeUp" ) } );
    world.context().entityTagRegister().tag( treeEntity, { gie::stringHasher( "TreeDown" ) } );

    std::string name = treeEntity ? std::string( gie::stringRegister().get( treeEntity->nameHash() ) ) : "unknown";
    return name + " (integrity: " + std::to_string( static_cast<int>( *integrityPpt->getFloat() ) ) + ")";
}

static std::string ExecuteBuildHouse( gie::World& world, gie::Agent* agent )
{
    auto siteSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "ConstructionSite" ) );
    if( siteSet && !siteSet->empty() )
    {
        glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();
        auto wpSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "Waypoint" ) );
        std::vector<gie::Guid> waypointGuids;
        if( wpSet ) waypointGuids.assign( wpSet->begin(), wpSet->end() );

        for( auto g : *siteSet )
        {
            auto e = world.entity( g );
            if( !e ) continue;
            auto loc = e->property( "Location" );
            if( !loc ) continue;
            auto path = gie::getPath( world, waypointGuids, agentLoc, *loc->getVec3() );
            g_LastActionPath.clear();
            g_LastActionPath.push_back( agentLoc );
            for( auto& wpG : path.path )
                if( auto wp = world.entity( wpG ) )
                    if( auto wl = wp->property( "Location" ) )
                        g_LastActionPath.push_back( *wl->getVec3() );
            g_LastActionPath.push_back( *loc->getVec3() );
            *agent->property( "Location" )->getVec3() = *loc->getVec3();
            break;
        }
    }

    auto treeDownSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "TreeDown" ) );
    if( !treeDownSet ) return "no logs available";

    std::vector<gie::Guid> toConsume;
    for( auto guid : *treeDownSet )
    {
        toConsume.push_back( guid );
        if( static_cast<int32_t>( toConsume.size() ) >= kMinLogsForHouse ) break;
    }
    for( auto guid : toConsume )
    {
        auto tree = world.entity( guid );
        if( tree )
        {
            world.context().entityTagRegister().untag( tree, { gie::stringHasher( "TreeDown" ) } );
            world.context().entityTagRegister().tag( tree, { gie::stringHasher( "TreeUsed" ) } );
        }
    }
    agent->property( "WoodHouse" )->value = true;
    return "built house (" + std::to_string( toConsume.size() ) + " logs consumed)";
}

static std::string ExecuteBuyThing( gie::World& world, gie::Agent* agent )
{
    auto moneyPpt = agent->property( "Money" );
    auto integrityPpt = agent->property( "AxeIntegrity" );
    moneyPpt->value = *moneyPpt->getFloat() - kAxePrice;
    integrityPpt->value = kNewAxeIntegrity;
    agent->property( "ThingsToBuy" )->getGuidArray()->clear();
    return "bought axe ($" + std::to_string( static_cast<int>( kAxePrice ) ) + ")";
}

static std::string ExecuteWork( gie::World& world, gie::Agent* agent )
{
    // Move to workplace
    auto workSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "Workplace" ) );
    if( workSet && !workSet->empty() )
    {
        glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();
        auto wpSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "Waypoint" ) );
        std::vector<gie::Guid> waypointGuids;
        if( wpSet ) waypointGuids.assign( wpSet->begin(), wpSet->end() );

        for( auto g : *workSet )
        {
            auto e = world.entity( g );
            if( !e ) continue;
            auto loc = e->property( "Location" );
            if( !loc ) continue;
            auto path = gie::getPath( world, waypointGuids, agentLoc, *loc->getVec3() );
            g_LastActionPath.clear();
            g_LastActionPath.push_back( agentLoc );
            for( auto& wpG : path.path )
                if( auto wp = world.entity( wpG ) )
                    if( auto wl = wp->property( "Location" ) )
                        g_LastActionPath.push_back( *wl->getVec3() );
            g_LastActionPath.push_back( *loc->getVec3() );
            *agent->property( "Location" )->getVec3() = *loc->getVec3();
            break;
        }
    }

    auto moneyPpt = agent->property( "Money" );
    moneyPpt->value = *moneyPpt->getFloat() + kWorkSalary;
    return "earned $" + std::to_string( static_cast<int>( kWorkSalary ) );
}

static std::string ExecuteEatFood( gie::World& world, gie::Agent* agent )
{
    auto foodSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "FoodSource" ) );
    if( !foodSet || foodSet->empty() ) return "no food source";

    glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();
    auto wpSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "Waypoint" ) );
    std::vector<gie::Guid> waypointGuids;
    if( wpSet ) waypointGuids.assign( wpSet->begin(), wpSet->end() );

    gie::Entity* bestFood = nullptr;
    float bestLen = std::numeric_limits<float>::max();
    gie::PathfindingResult bestPath;
    for( auto g : *foodSet )
    {
        auto e = world.entity( g );
        if( !e ) continue;
        auto loc = e->property( "Location" );
        if( !loc ) continue;
        auto candidate = gie::getPath( world, waypointGuids, agentLoc, *loc->getVec3() );
        if( candidate.length < bestLen ) { bestLen = candidate.length; bestFood = e; bestPath = std::move( candidate ); }
    }
    if( !bestFood ) return "";

    g_LastActionPath.clear();
    g_LastActionPath.push_back( agentLoc );
    for( auto& wpG : bestPath.path )
        if( auto wp = world.entity( wpG ) )
            if( auto wl = wp->property( "Location" ) )
                g_LastActionPath.push_back( *wl->getVec3() );
    auto foodLoc = bestFood->property( "Location" );
    if( foodLoc ) { g_LastActionPath.push_back( *foodLoc->getVec3() ); *agent->property( "Location" )->getVec3() = *foodLoc->getVec3(); }

    auto hunger = agent->property( "Hunger" );
    float before = *hunger->getFloat();
    hunger->value = clampf( before - 60.f, 0.f, kMaxHunger );
    return "hunger " + std::to_string( static_cast<int>( before ) ) + " -> " + std::to_string( static_cast<int>( *hunger->getFloat() ) );
}

static std::string ExecuteDrinkWater( gie::World& world, gie::Agent* agent )
{
    auto waterSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "WaterSource" ) );
    if( !waterSet || waterSet->empty() ) return "no water source";

    glm::vec3 agentLoc = *agent->property( "Location" )->getVec3();
    auto wpSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "Waypoint" ) );
    std::vector<gie::Guid> waypointGuids;
    if( wpSet ) waypointGuids.assign( wpSet->begin(), wpSet->end() );

    gie::Entity* bestWater = nullptr;
    float bestLen = std::numeric_limits<float>::max();
    gie::PathfindingResult bestPath;
    for( auto g : *waterSet )
    {
        auto e = world.entity( g );
        if( !e ) continue;
        auto loc = e->property( "Location" );
        if( !loc ) continue;
        auto candidate = gie::getPath( world, waypointGuids, agentLoc, *loc->getVec3() );
        if( candidate.length < bestLen ) { bestLen = candidate.length; bestWater = e; bestPath = std::move( candidate ); }
    }
    if( !bestWater ) return "";

    g_LastActionPath.clear();
    g_LastActionPath.push_back( agentLoc );
    for( auto& wpG : bestPath.path )
        if( auto wp = world.entity( wpG ) )
            if( auto wl = wp->property( "Location" ) )
                g_LastActionPath.push_back( *wl->getVec3() );
    auto waterLoc = bestWater->property( "Location" );
    if( waterLoc ) { g_LastActionPath.push_back( *waterLoc->getVec3() ); *agent->property( "Location" )->getVec3() = *waterLoc->getVec3(); }

    auto thirst = agent->property( "Thirst" );
    float before = *thirst->getFloat();
    thirst->value = clampf( before - 70.f, 0.f, kMaxThirst );
    return "thirst " + std::to_string( static_cast<int>( before ) ) + " -> " + std::to_string( static_cast<int>( *thirst->getFloat() ) );
}

static std::string ExecuteSleep( gie::World& world, gie::Agent* agent )
{
    auto energy = agent->property( "Energy" );
    float before = *energy->getFloat();
    energy->value = clampf( before + 60.f, 0.f, kMaxEnergy );

    auto hunger = agent->property( "Hunger" );
    hunger->value = clampf( *hunger->getFloat() + 5.f, 0.f, kMaxHunger );
    auto thirst = agent->property( "Thirst" );
    thirst->value = clampf( *thirst->getFloat() + 5.f, 0.f, kMaxThirst );

    return "energy " + std::to_string( static_cast<int>( before ) ) + " -> " + std::to_string( static_cast<int>( *energy->getFloat() ) );
}

static std::string ExecuteAction( gie::World& world, gie::Agent* agent, const std::string& actionName )
{
    if( actionName == "CutDownTree" )     return ExecuteCutDownTree( world, agent );
    else if( actionName == "BuildHouse" ) return ExecuteBuildHouse( world, agent );
    else if( actionName == "BuyThing" )   return ExecuteBuyThing( world, agent );
    else if( actionName == "Work" )       return ExecuteWork( world, agent );
    else if( actionName == "EatFood" )    return ExecuteEatFood( world, agent );
    else if( actionName == "DrinkWater" ) return ExecuteDrinkWater( world, agent );
    else if( actionName == "Sleep" )      return ExecuteSleep( world, agent );
    else if( actionName == "NewThingToBuy" ) return ""; // planning-internal signal
    return "";
}

// ---------------------------------------------------------------------------
// Gameplay loop
// ---------------------------------------------------------------------------
static CycleResult RunGameplayCycle( gie::World& world, gie::Agent* agent )
{
    auto woodHousePpt = agent->property( "WoodHouse" );
    if( woodHousePpt && *woodHousePpt->getBool() )
    {
        g_GameplayLog.primaryGoalReached = true;
        return CycleResult::GoalReached;
    }

    int cycleNum = static_cast<int>( g_GameplayLog.cycles.size() ) + 1;
    GameplayCycleEntry entry;
    entry.cycle = cycleNum;
    entry.agentPosBefore = *agent->property( "Location" )->getVec3();

    // Plan for primary goal: WoodHouse=true
    gie::Goal primaryGoal{ world };
    primaryGoal.targets.emplace_back( woodHousePpt->guid(), true );

    gie::Planner cyclePlanner{};
    RegisterActions( cyclePlanner );
    cyclePlanner.depthLimitMutator() = 16;
    cyclePlanner.simulate( primaryGoal, *agent );
    cyclePlanner.plan( true ); // use heuristics

    auto& planned = cyclePlanner.planActions();
    if( !planned.empty() )
    {
        entry.goalType = "Primary";
        entry.planFound = true;
        entry.simulationCount = cyclePlanner.simulations().size();
        for( int i = static_cast<int>( planned.size() ) - 1; i >= 0; i-- )
            entry.actionNames.push_back( std::string( planned[i]->name() ) );

        printf( "  Cycle %d: %zu actions —", cycleNum, entry.actionNames.size() );
        std::vector<std::vector<glm::vec3>> cyclePaths;
        for( auto& name : entry.actionNames )
        {
            g_LastActionPath.clear();
            entry.actionDetails.push_back( ExecuteAction( world, agent, name ) );
            cyclePaths.push_back( std::move( g_LastActionPath ) );
            printf( " %s", name.c_str() );
        }
        printf( "\n" );

        entry.agentPosAfter = *agent->property( "Location" )->getVec3();
        g_GameplayLog.agentTrail.push_back( entry.agentPosAfter );
        g_GameplayLog.cycles.push_back( std::move( entry ) );
        g_CycleActionPaths.push_back( std::move( cyclePaths ) );

        if( *agent->property( "WoodHouse" )->getBool() )
        {
            g_GameplayLog.primaryGoalReached = true;
            return CycleResult::GoalReached;
        }
        return CycleResult::Continued;
    }

    // Stuck — no plan found
    entry.goalType = "Stuck";
    entry.planFound = false;
    entry.agentPosAfter = entry.agentPosBefore;
    g_GameplayLog.cycles.push_back( std::move( entry ) );
    return CycleResult::Stuck;
}

static void RunGameplayLoop( gie::World& world, gie::Agent* agent, gie::Planner& planner )
{
    g_GameplayLog = {};
    g_CycleActionPaths.clear();
    g_LastActionPath.clear();
    g_GameplayLog.started = true;
    g_GameplayLog.agentTrail.push_back( *agent->property( "Location" )->getVec3() );

    const int maxCycles = 30;
    for( int i = 0; i < maxCycles; i++ )
    {
        CycleResult result = RunGameplayCycle( world, agent );
        if( result != CycleResult::Continued )
            break;
    }
}

int survivalOnHillValidateResult( std::string& failMsg )
{
	gie::World world{};
	gie::Planner planner{};
	gie::Goal goal{ world };
	ExampleParameters params{ world, planner, goal };

	VALIDATE( survivalOnHill( params ) == 0, "survivalOnHill() setup failed" );

	VALIDATE_EQ( planner.actionSet().size(), size_t( 7 ), "action set entry count" );
	VALIDATE( !goal.targets.empty(), "goal should have at least one target" );
	VALIDATE( planner.agent() != nullptr, "planner should have an assigned agent" );

	// Gameplay loop should have reached the goal
	VALIDATE( g_GameplayLog.primaryGoalReached, "primary goal should be reached" );
	VALIDATE( !g_GameplayLog.cycles.empty(), "should have at least one planning cycle" );

	// Check WoodHouse is true
	auto agentEnt = world.entity( planner.agent()->guid() );
	VALIDATE( agentEnt != nullptr, "agent should exist" );
	VALIDATE( *agentEnt->property( "WoodHouse" )->getBool(), "WoodHouse should be true" );

	// Check BuildHouse appears in the log
	bool hasBuildHouse = false;
	for( auto& c : g_GameplayLog.cycles )
		for( auto& a : c.actionNames )
			if( a == "BuildHouse" ) { hasBuildHouse = true; break; }
	VALIDATE( hasBuildHouse, "plan should include BuildHouse" );

	// Debug output
	printf( "  Gameplay cycles: %zu, goalReached: %s\n", g_GameplayLog.cycles.size(), g_GameplayLog.primaryGoalReached ? "yes" : "no" );
	for( size_t ci = 0; ci < g_GameplayLog.cycles.size(); ci++ )
	{
		auto& c = g_GameplayLog.cycles[ci];
		printf( "  Cycle %d [%s] sims=%zu actions=%zu\n",
			c.cycle, c.goalType.c_str(), c.simulationCount, c.actionNames.size() );
		for( size_t ai = 0; ai < c.actionNames.size(); ai++ )
			printf( "    %zu. %s — %s\n", ai + 1, c.actionNames[ai].c_str(),
				ai < c.actionDetails.size() ? c.actionDetails[ai].c_str() : "" );
	}

	return 0;
}

inline float remapRange( float source, float sourceFrom, float sourceTo, float targetFrom, float targetTo )
{
    return targetFrom + ( source - sourceFrom ) * ( targetTo - targetFrom ) / ( sourceTo - sourceFrom );
}

// ---------------------------------------------------------------------------
// GL Draw — agent trail and per-action movement paths
// ---------------------------------------------------------------------------
static void GLDrawFunc5( gie::World& world, gie::Planner& planner )
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

    // Draw resource markers
    // Food sources: green circle
    auto foodSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "FoodSource" ) );
    if( foodSet )
    {
        glPointSize( 10.0f );
        glColor3f( 0.3f, 0.9f, 0.3f );
        glBegin( GL_POINTS );
        for( auto g : *foodSet )
        {
            auto e = world.entity( g );
            if( !e ) continue;
            auto loc = e->property( "Location" );
            if( !loc ) continue;
            glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
            glVertex3f( p.x, p.y, p.z );
        }
        glEnd();
    }

    // Water sources: blue circle
    auto waterSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "WaterSource" ) );
    if( waterSet )
    {
        glPointSize( 10.0f );
        glColor3f( 0.2f, 0.5f, 1.0f );
        glBegin( GL_POINTS );
        for( auto g : *waterSet )
        {
            auto e = world.entity( g );
            if( !e ) continue;
            auto loc = e->property( "Location" );
            if( !loc ) continue;
            glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
            glVertex3f( p.x, p.y, p.z );
        }
        glEnd();
    }

    // Workplace: orange triangle
    auto workSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "Workplace" ) );
    if( workSet )
    {
        for( auto g : *workSet )
        {
            auto e = world.entity( g );
            if( !e ) continue;
            auto loc = e->property( "Location" );
            if( !loc ) continue;
            glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
            glColor3f( 1.0f, 0.6f, 0.0f );
            float s = 0.02f;
            glBegin( GL_TRIANGLES );
            glVertex3f( p.x, p.y + s, p.z );
            glVertex3f( p.x - s, p.y - s, p.z );
            glVertex3f( p.x + s, p.y - s, p.z );
            glEnd();
        }
    }

    // Construction site: yellow square
    auto siteSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "ConstructionSite" ) );
    if( siteSet )
    {
        for( auto g : *siteSet )
        {
            auto e = world.entity( g );
            if( !e ) continue;
            auto loc = e->property( "Location" );
            if( !loc ) continue;
            glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
            bool built = false;
            if( planner.agent() )
            {
                auto wh = world.entity( planner.agent()->guid() );
                if( wh ) built = *wh->property( "WoodHouse" )->getBool();
            }
            if( built ) glColor3f( 0.0f, 1.0f, 0.0f );
            else        glColor3f( 1.0f, 0.9f, 0.0f );
            float s = 0.02f;
            glBegin( GL_QUADS );
            glVertex3f( p.x - s, p.y - s, p.z );
            glVertex3f( p.x + s, p.y - s, p.z );
            glVertex3f( p.x + s, p.y + s, p.z );
            glVertex3f( p.x - s, p.y + s, p.z );
            glEnd();
        }
    }

    // Agent marker (magenta dot)
    if( planner.agent() )
    {
        auto agentEnt = world.entity( planner.agent()->guid() );
        if( agentEnt )
        {
            auto agentLoc = agentEnt->property( "Location" );
            if( agentLoc )
            {
                glm::vec3 p = ( *agentLoc->getVec3() + offset ) * scale;
                glPointSize( 12.0f );
                glColor3f( 1.0f, 0.0f, 1.0f );
                glBegin( GL_POINTS );
                glVertex3f( p.x, p.y, p.z );
                glEnd();
            }
        }
    }
}

static void ImGuiFunc5( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimulationGuid )
{
    ImGui::TextUnformatted( "Survival on Hill - Gameplay Loop (Example 5)" );
    ImGui::Separator();

    // context comes from either a simulation or the world
    gie::Blackboard* context = nullptr;
    const auto selectedSimulation = planner.simulation( selectedSimulationGuid );
    if( selectedSimulation )
        context = &selectedSimulation->context();
    if( !context )
        context = &world.context();

    const auto agentEntity = context->entity( planner.agent()->guid() );
    if( !agentEntity ) return;

    auto agentLocationPpt = agentEntity->property( "Location" );
    if( agentLocationPpt )
    {
        glm::vec3 agentLocation = *agentLocationPpt->getVec3();
        ImGui::Text( "Agent Location: (%.1f, %.1f, %.1f)", agentLocation.x, agentLocation.y, agentLocation.z );
    }
    if( auto p = agentEntity->property( "Energy" ) ) ImGui::Text( "Energy: %.1f", *p->getFloat() );
    if( auto p = agentEntity->property( "Hunger" ) ) ImGui::Text( "Hunger: %.1f", *p->getFloat() );
    if( auto p = agentEntity->property( "Thirst" ) ) ImGui::Text( "Thirst: %.1f", *p->getFloat() );
    ImGui::Text( "Money: $%.0f", *agentEntity->property( "Money" )->getFloat() );
    ImGui::Text( "Axe Integrity: %.0f", *agentEntity->property( "AxeIntegrity" )->getFloat() );
    ImGui::Text( "Wood House: %s", *agentEntity->property( "WoodHouse" )->getBool() ? "YES" : "NO" );

    // Tree counts
    int treeUpCount = 0, treeDownCount = 0, treeUsedCount = 0;
    if( auto s = context->entityTagRegister().tagSet( "TreeUp" ) ) treeUpCount = static_cast<int>( s->size() );
    if( auto s = context->entityTagRegister().tagSet( "TreeDown" ) ) treeDownCount = static_cast<int>( s->size() );
    if( auto s = context->entityTagRegister().tagSet( "TreeUsed" ) ) treeUsedCount = static_cast<int>( s->size() );

    int foodCount = 0, waterCount = 0;
    if( auto s = context->entityTagRegister().tagSet( "FoodSource" ) ) foodCount = static_cast<int>( s->size() );
    if( auto s = context->entityTagRegister().tagSet( "WaterSource" ) ) waterCount = static_cast<int>( s->size() );

    ImGui::Text( "Trees: %d up, %d down (logs), %d used", treeUpCount, treeDownCount, treeUsedCount );
    ImGui::Text( "Food Sources: %d, Water Sources: %d", foodCount, waterCount );

    ImGui::Separator();

    // Gameplay controls
    bool finished = g_GameplayLog.primaryGoalReached
        || ( !g_GameplayLog.cycles.empty() && g_GameplayLog.cycles.back().goalType == "Stuck" );

    static bool stepExecution = false;

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
                    RunGameplayLoop( world, agentPtr, planner );
                }
            }
        }
        ImGui::PopStyleColor( 2 );
    }
    else if( stepExecution && !finished )
    {
        if( ImGui::Button( "Step (Next Cycle)", ImVec2( -1.f, 30.f ) ) )
        {
            RunGameplayCycle( world, planner.agent() );
        }
    }

    // Goal status
    if( g_GameplayLog.primaryGoalReached )
        ImGui::TextColored( ImVec4( 0.2f, 1.0f, 0.2f, 1.0f ), "Goal Reached: Wood House Built!" );
    else if( finished )
        ImGui::TextColored( ImVec4( 1.0f, 0.3f, 0.3f, 1.0f ), "Stuck: No plan found" );

    // Gameplay log
    if( !g_GameplayLog.cycles.empty() )
    {
        ImGui::Separator();
        if( ImGui::CollapsingHeader( "Gameplay Log", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            for( size_t ci = 0; ci < g_GameplayLog.cycles.size(); ci++ )
            {
                auto& cycle = g_GameplayLog.cycles[ci];
                bool selected = ( g_GameplayLog.selectedCycle == static_cast<int>( ci ) );
                if( selected ) ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.4f, 0.9f, 1.0f, 1.0f ) );

                bool open = ImGui::TreeNode( reinterpret_cast<void*>( static_cast<intptr_t>( cycle.cycle ) ),
                    "Cycle %d [%s] (%zu actions, %zu sims)",
                    cycle.cycle, cycle.goalType.c_str(),
                    cycle.actionNames.size(), cycle.simulationCount );

                // Click to select/deselect cycle for highlight
                if( ImGui::IsItemClicked() )
                    g_GameplayLog.selectedCycle = selected ? -1 : static_cast<int>( ci );

                if( selected ) ImGui::PopStyleColor();

                if( open )
                {
                    for( size_t i = 0; i < cycle.actionNames.size(); i++ )
                    {
                        ImGui::Text( "%zu. %s", i + 1, cycle.actionNames[i].c_str() );
                        if( i < cycle.actionDetails.size() && !cycle.actionDetails[i].empty() )
                        {
                            ImGui::SameLine();
                            ImGui::TextColored( ImVec4( 0.7f, 0.7f, 0.7f, 1.0f ), "- %s", cycle.actionDetails[i].c_str() );
                        }
                    }
                    ImGui::TreePop();
                }
            }
        }
    }
}
