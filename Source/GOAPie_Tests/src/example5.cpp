#include <functional>
#include <array>
#include <algorithm>

#include <goapie.h>

#include "example.h"
#include "waypoint_navigation.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

static inline float clampf( float v, float lo, float hi ) { return std::max( lo, std::min( v, hi ) ); }

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

int survivalOnHill( ExampleParameters& params )
{
    gie::World& world = params.world;
    gie::Planner& planner = params.planner;
    gie::Goal& goal = params.goal;

    // specific example visualization
    params.imGuiDrawFunc = &ImGuiFunc5;

    // creating agent (aka npc)
    auto agentEntity = world.createAgent( "Pawn" );

    // registering agent with tag to be found later in simulation
    world.context().entityTagRegister().tag( agentEntity, { gie::stringHasher( "Agent" ) } );

    // property telling if agent has a wood house
    auto agentWoodHousePpt = agentEntity->createProperty( "WoodHouse", false );

    // creating agent properties for this tutorial

    // total money npc carries
    agentEntity->createProperty( "Money", 0.f );
    // things (e.g. axe) npc need to buy
    agentEntity->createProperty( "ThingsToBuy", gie::Property::GuidVector{} );
    // integrity of axe npc is carrying
    agentEntity->createProperty( "AxeIntegrity", 0.f );
    // initial position of agent
    agentEntity->createProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );

    // survival stats
    static constexpr float kMaxEnergy = 100.f;
    static constexpr float kMaxHunger = 100.f; // higher == more hungry
    static constexpr float kMaxThirst = 100.f; // higher == more thirsty

    agentEntity->createProperty( "Energy", kMaxEnergy );
    agentEntity->createProperty( "Hunger", 20.f );
    agentEntity->createProperty( "Thirst", 20.f );

    // thresholds
    static constexpr float kMinEnergyToAct = 20.f;
    static constexpr float kHungerHigh = 70.f;
    static constexpr float kThirstHigh = 70.f;

    // per-unit path multipliers for survival deltas
    static constexpr float kEnergyPerPath = 0.25f;  // energy consumed per path length unit
    static constexpr float kHungerPerPath = 0.15f;  // hunger increases per path unit
    static constexpr float kThirstPerPath = 0.2f;   // thirst increases per path unit

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
    for( size_t i = 0; i < waypointCount; i++ )
    {
        auto& waypointLocation = waypointLocations[ i ];
        auto wypointEntityName = std::string( "waypoint" + std::to_string( i ) );
        auto& createdWaypoint = waypoints.emplace_back( world.createEntity( wypointEntityName ) );
        world.context().entityTagRegister().tag( createdWaypoint, { gie::stringHasher( "Waypoint" ) } );
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

    // price to get an axe
    constexpr float axePrice = 15.f;

    // creating entity defining axe (a thing) npc can buy
    auto axeInfoEntity = world.createEntity( "AxeInfo" );
    axeInfoEntity->createProperty( "Name", "Axe" );
    axeInfoEntity->createProperty( "Price", axePrice );

    // registering entity with tag to be found later in simulation
    world.context().entityTagRegister().tag( axeInfoEntity, { gie::stringHasher( "AxeInfo" ) } );

    // money earn from work
    constexpr float workSalary = 20.0;
    // brand new axe integrity 
    static constexpr float newAxeIntegrityValue = 3.f;

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

    DEFINE_DUMMY_ACTION_CLASS( CutDownTree )

    // minimal amount of integrity an axe need to have to cut down a tree, so there is no need to buy another one
    constexpr float minAxeIntegrity = 1.f;

    // defining simulator for action cut down tree
    class CutDownTreeSimulator : public gie::ActionSimulator
    {
    public:
        using gie::ActionSimulator::ActionSimulator;
        gie::StringHash hash() const override { return gie::stringRegister().add( "CutDownTree" ); }

        // define conditions for action
        bool evaluate( gie::EvaluateSimulationParams params ) const override
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
            if( *axeIntegrityPpt->getFloat() < minAxeIntegrity )
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
        }

        bool simulate( gie::SimulateSimulationParams params ) const override
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

            // creating cut down tree action
            if( auto cutDownTreeAction = std::make_shared< CutDownTreeAction >() )
            {
                cutDownTreeAction->arguments().add( { gie::stringHasher( "TargetTree" ), chosenTreeGuid } );
                params.simulation.actions.emplace_back( cutDownTreeAction );
                params.addDebugMessage( "CutDownTreeAction added, returning TRUE" );
                return true;
            }

            params.addDebugMessage( "CutDownTreeAction not added, returning FALSE" );
            return false;
        }

        void calculateHeuristic( gie::CalculateHeuristicParams params ) const override
        {
            // default heuristic: path length from agent to nearest tree
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
    };

    DEFINE_DUMMY_ACTION_CLASS( NewThingToBuy )
    DEFINE_DUMMY_ACTION_CLASS( BuyThing )
    DEFINE_DUMMY_ACTION_CLASS( BuildHouse )

    // minimum logs (cut down trees) required to build a house
    constexpr int32_t minLogsForHouse = 5;

    class BuildHouseSimulator : public gie::ActionSimulator
    {
    public:
        using gie::ActionSimulator::ActionSimulator;
        gie::StringHash hash() const override
        {
            return gie::stringRegister().add( "BuildHouse" );
        }

        bool evaluate( gie::EvaluateSimulationParams params ) const override
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
            params.addDebugMessage( "Required logs: " + std::to_string( minLogsForHouse ) );

            // check if we have enough logs to build a house
            if( availableLogs >= minLogsForHouse )
            {
                params.addDebugMessage( "Enough logs available to build house, returning TRUE" );
                return true;
            }

            params.addDebugMessage( "Not enough logs to build house, returning FALSE" );
            return false;
        }

        bool simulate( gie::SimulateSimulationParams params ) const override
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

            if( availableLogs >= minLogsForHouse )
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
                    if( logsConsumed >= minLogsForHouse )
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

                // creating build house action
                if( auto buildHouseAction = std::make_shared< BuildHouseAction >() )
                {
                    params.simulation.actions.emplace_back( buildHouseAction );
                    params.addDebugMessage( "BuildHouseAction added, house built successfully! Returning TRUE" );
                    return true;
                }
            }

            params.addDebugMessage( "BuildHouseAction not added, returning FALSE" );
            return false;
        }
    };

    class BuyThingSimulator : public gie::ActionSimulator
    {
    public:
        using gie::ActionSimulator::ActionSimulator;
        gie::StringHash hash() const override
        {
            return gie::stringRegister().add( "BuyThing" );
        }

        bool evaluate( gie::EvaluateSimulationParams params ) const override
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
                const bool hasEnoughMoney = money >= axePrice;
                params.addDebugMessage(
                    "Money: " + std::to_string( money ) + ", axe price: " + std::to_string( axePrice )
                    + ", has enough money: " + ( hasEnoughMoney ? "TRUE" : "FALSE" ) );
                return hasEnoughMoney;
            }

            params.addDebugMessage( "Axe is not in things to buy, returning FALSE" );
            return false;
        }

        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            auto& context = params.simulation.context();
            auto agentEntity = context.entity( params.agent.guid() );
            auto thingsToBuyPpt = agentEntity->property( "ThingsToBuy" );
            auto axeInfoTagSet = context.entityTagRegister().tagSet( "AxeInfo" );
            auto axeInfoEntityGuid = *axeInfoTagSet->cbegin();
            auto moneyPpt = agentEntity->property( "Money" );

            params.addDebugMessage( "BuyThingSimulator::simulate" );

            // buy axe if there is enough money
            if( *moneyPpt->getFloat() >= axePrice )
            {
                auto axeIntegrityPpt = agentEntity->property( "AxeIntegrity" );
                axeIntegrityPpt->value = newAxeIntegrityValue;

                // consuming money
                moneyPpt->value = *moneyPpt->getFloat() - axePrice;

                // removing axe from things to buy property
                auto thingsToBuyArray = thingsToBuyPpt->getGuidArray();
                auto newArrayEnd = std::remove( thingsToBuyArray->begin(), thingsToBuyArray->end(), axeInfoEntityGuid );

                thingsToBuyArray->erase( newArrayEnd, thingsToBuyArray->end() );

                // apply a small baseline fatigue for the purchase logistics (no travel path here)
                float zeroPath = 0.f;
                ApplyTravelAndBaseDeltas( agentEntity, /*baseEnergy*/ 2.f, /*baseHunger*/ 1.f, /*baseThirst*/ 1.f, zeroPath, kEnergyPerPath, kHungerPerPath, kThirstPerPath, kMaxEnergy, kMaxHunger, kMaxThirst, params, "BuyThing" );

                // creating buy axe action
                if( auto buyThingAction = std::make_shared< BuyThingAction >( arguments() ) )
                {
                    params.simulation.actions.emplace_back( buyThingAction );
                    params.addDebugMessage( "BuyThingAction added, returning TRUE" );
                    return true;
                }
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

                if( auto raiseMoneyNeededAction = std::make_shared< NewThingToBuyAction >( actionArguments ) )
                {
                    params.simulation.actions.emplace_back( raiseMoneyNeededAction );
                    params.addDebugMessage( "NewThingToBuyAction added, returning TRUE" );
                    return true;
                }
            }

            return false;
        }
    };

    DEFINE_DUMMY_ACTION_CLASS( Work )

    class WorkSimulator : public gie::ActionSimulator
    {
    public:
        using gie::ActionSimulator::ActionSimulator;
        gie::StringHash hash() const override { return gie::stringRegister().add( "Work" ); }

        // define conditions for action
        bool evaluate( gie::EvaluateSimulationParams params ) const override
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
        }

        // calculate cost and necessary steps (other actions) to achieve the action being simulated
        bool simulate( gie::SimulateSimulationParams params ) const override
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
            moneyPpt->value = *moneyPpt->getFloat() + workSalary;

            // apply travel and base deltas
            ApplyTravelAndBaseDeltas( agentEntity, /*baseEnergy*/ 6.f, /*baseHunger*/ 5.f, /*baseThirst*/ 6.f, pathLen, kEnergyPerPath, kHungerPerPath, kThirstPerPath, kMaxEnergy, kMaxHunger, kMaxThirst, params, "Work" );

            params.addDebugMessage( "WorkSimulator::simulate" );
            params.addDebugMessage( "Working, adding " + std::to_string( workSalary ) + " to money" );

            // creating work action
            if( auto workAction = std::make_shared< WorkAction >( arguments() ) )
            {
                params.simulation.actions.emplace_back( workAction );
                params.addDebugMessage( "WorkAction added, returning TRUE" );
                return true;
            }

            params.addDebugMessage( "WorkAction not added, returning FALSE" );
            return false;
        }
    };

    // Survival actions
    DEFINE_DUMMY_ACTION_CLASS( EatFood )
    DEFINE_DUMMY_ACTION_CLASS( DrinkWater )
    DEFINE_DUMMY_ACTION_CLASS( Sleep )

    class EatFoodSimulator : public gie::ActionSimulator
    {
    public:
        using gie::ActionSimulator::ActionSimulator;
        gie::StringHash hash() const override { return gie::stringRegister().add( "EatFood" ); }

        bool evaluate( gie::EvaluateSimulationParams params ) const override
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
        }

        bool simulate( gie::SimulateSimulationParams params ) const override
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

            if( auto act = std::make_shared< EatFoodAction >() )
            {
                act->arguments().add( { gie::stringHasher( "Target" ), bestFood } );
                params.simulation.actions.emplace_back( act );
                params.addDebugMessage( "EatFoodAction added, returning TRUE" );
                return true;
            }
            params.addDebugMessage( "EatFoodAction not added, returning FALSE" );
            return false;
        }
    };

    class DrinkWaterSimulator : public gie::ActionSimulator
    {
    public:
        using gie::ActionSimulator::ActionSimulator;
        gie::StringHash hash() const override { return gie::stringRegister().add( "DrinkWater" ); }

        bool evaluate( gie::EvaluateSimulationParams params ) const override
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
        }

        bool simulate( gie::SimulateSimulationParams params ) const override
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

            if( auto act = std::make_shared< DrinkWaterAction >() )
            {
                act->arguments().add( { gie::stringHasher( "Target" ), bestWater } );
                params.simulation.actions.emplace_back( act );
                params.addDebugMessage( "DrinkWaterAction added, returning TRUE" );
                return true;
            }
            params.addDebugMessage( "DrinkWaterAction not added, returning FALSE" );
            return false;
        }
    };

    class SleepSimulator : public gie::ActionSimulator
    {
    public:
        using gie::ActionSimulator::ActionSimulator;
        gie::StringHash hash() const override { return gie::stringRegister().add( "Sleep" ); }

        bool evaluate( gie::EvaluateSimulationParams params ) const override
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
        }

        bool simulate( gie::SimulateSimulationParams params ) const override
        {
            auto& ctx = params.simulation.context();
            auto agent = ctx.entity( params.agent.guid() );
            params.addDebugMessage( "SleepSimulator::simulate" );

            // No travel for sleeping (uses current location) but still increases hunger and thirst slightly over time
            float pathLen = 0.f;
            ApplyTravelAndBaseDeltas( agent, /*baseEnergy*/ -60.f, /*baseHunger*/ 5.f, /*baseThirst*/ 5.f, pathLen, kEnergyPerPath, kHungerPerPath, kThirstPerPath, kMaxEnergy, kMaxHunger, kMaxThirst, params, "Sleep(rest)" );

            if( auto act = std::make_shared< SleepAction >() )
            {
                params.simulation.actions.emplace_back( act );
                params.addDebugMessage( "SleepAction added, returning TRUE" );
                return true;
            }
            params.addDebugMessage( "SleepAction not added, returning FALSE" );
            return false;
        }
    };

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

    // adding trees to world
    for( size_t i = 0; i < treeCount; i++ )
    {
        auto treeEntity = world.createEntity( std::string( "tree" + std::to_string( i ) ) );
        treeEntity->createProperty( "Location", treeLocations[ i ] );
        world.context().entityTagRegister().tag( treeEntity, { gie::stringHasher( "Tree" ), gie::stringHasher( "TreeUp" ) } );
    }

    // setting up planner passing goal and agent to reach the goal
    planner.simulate( goal, *agentEntity );

    // defining available actions and their simulators for planner
    DEFINE_ACTION_SET_ENTRY( CutDownTree )
    DEFINE_ACTION_SET_ENTRY( BuyThing )
    DEFINE_ACTION_SET_ENTRY( Work )
    DEFINE_ACTION_SET_ENTRY( BuildHouse )
    DEFINE_ACTION_SET_ENTRY( EatFood )
    DEFINE_ACTION_SET_ENTRY( DrinkWater )
    DEFINE_ACTION_SET_ENTRY( Sleep )

    // setting available actions
    planner.addActionSetEntry< CutDownTreeActionSetEntry >( gie::stringHasher( "CutDownTree" ) );
    planner.addActionSetEntry< BuyThingActionSetEntry >( gie::stringHasher( "BuyThing" ) );
    planner.addActionSetEntry< WorkActionSetEntry >( gie::stringHasher( "Work" ) );
    planner.addActionSetEntry< BuildHouseActionSetEntry >( gie::stringHasher( "BuildHouse" ) );
    planner.addActionSetEntry< EatFoodActionSetEntry >( gie::stringHasher( "EatFood" ) );
    planner.addActionSetEntry< DrinkWaterActionSetEntry >( gie::stringHasher( "DrinkWater" ) );
    planner.addActionSetEntry< SleepActionSetEntry >( gie::stringHasher( "Sleep" ) );

    return 0;
}

inline float remapRange( float source, float sourceFrom, float sourceTo, float targetFrom, float targetTo )
{
    return targetFrom + ( source - sourceFrom ) * ( targetTo - targetFrom ) / ( sourceTo - sourceFrom );
}

static void ImGuiFunc5( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimulationGuid )
{
    // context comes from either a simulation or the world
    gie::Blackboard* context = nullptr;

    const auto selectedSimulation = planner.simulation( selectedSimulationGuid );
    if( selectedSimulation )
    {
        context = &selectedSimulation->context();
    }

    if( !context )
    {
        context = &world.context();
    }

    // getting agent entity
    const auto agentEntity = context->entity( planner.agent()->guid() );
    if( !agentEntity )
    {
        return;
    }
    // getting agent location property
    auto agentLocationPpt = agentEntity->property( "Location" );
    if( !agentLocationPpt )
    {
        return;
    }

    glm::vec3 agentLocation = *agentLocationPpt->getVec3();
    ImGui::Text( "Agent Location: (%.2f, %.2f, %.2f)", agentLocation.x, agentLocation.y, agentLocation.z );
    if( auto p = agentEntity->property( "Energy" ) ) ImGui::Text( "Energy: %.1f", *p->getFloat() );
    if( auto p = agentEntity->property( "Hunger" ) ) ImGui::Text( "Hunger: %.1f", *p->getFloat() );
    if( auto p = agentEntity->property( "Thirst" ) ) ImGui::Text( "Thirst: %.1f", *p->getFloat() );
    ImGui::Text( "Agent Money: %.2f", *agentEntity->property( "Money" )->getFloat() );
    ImGui::Text( "Axe Integrity: %.2f", *agentEntity->property( "AxeIntegrity" )->getFloat() );
    ImGui::Text( "Has Wood House: %s", *agentEntity->property( "WoodHouse" )->getBool() ? "YES" : "NO" );
    
    // counting trees by their states
    int treeUpCount = 0;
    int treeDownCount = 0;
    int treeUsedCount = 0;
    
    auto treeUpTagSet = context->entityTagRegister().tagSet( "TreeUp" );
    if( treeUpTagSet )
    {
        treeUpCount = static_cast<int>( treeUpTagSet->size() );
    }
    
    auto treeDownTagSet = context->entityTagRegister().tagSet( "TreeDown" );
    if( treeDownTagSet )
    {
        treeDownCount = static_cast<int>( treeDownTagSet->size() );
    }
    
    auto treeUsedTagSet = context->entityTagRegister().tagSet( "TreeUsed" );
    if( treeUsedTagSet )
    {
        treeUsedCount = static_cast<int>( treeUsedTagSet->size() );
    }

    int foodCount = 0;
    int waterCount = 0;
    auto foodSet = context->entityTagRegister().tagSet( "FoodSource" );
    if( foodSet ) foodCount = static_cast<int>( foodSet->size() );
    auto waterSet = context->entityTagRegister().tagSet( "WaterSource" );
    if( waterSet ) waterCount = static_cast<int>( waterSet->size() );
    
    ImGui::Text( "Trees Up: %d, Trees Down (logs): %d, Trees Used: %d", treeUpCount, treeDownCount, treeUsedCount );
    ImGui::Text( "Food Sources: %d, Water Sources: %d", foodCount, waterCount );
    ImGui::Text( "Things to buy: " );

    auto thingsToBuyPpt = agentEntity->property( "ThingsToBuy" );
    if( thingsToBuyPpt )
    {
        auto thingsToBuyArray = thingsToBuyPpt->getGuidArray();
        for( const auto& thingGuid : *thingsToBuyArray )
        {
            if( auto thingEntity = context->entity( thingGuid ) )
            {
                auto thingNameHash = thingEntity->property( "Name" )->getStringHash();
                if( thingNameHash == gie::InvalidStringHash )
                {
                    continue;
                }
                auto thingName = gie::stringRegister().get( *thingNameHash );
                ImGui::Text( "- %s", thingName );
            }
        }
    }
}
