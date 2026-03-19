#include <algorithm>

#include <goapie.h>

#include "example.h"
#include "gameplay_common.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

extern void printSimulatedActions( const gie::Planner& planner );

const char* cutDownTreesDescription()
{
	return "Agent gathers logs by cutting down trees and may build a house when enough logs are available.";
}

// ---------------------------------------------------------------------------
// File-scope constants
// ---------------------------------------------------------------------------
static constexpr float kAxePrice = 15.f;
static constexpr float kWorkSalary = 20.f;
static constexpr float kNewAxeIntegrity = 3.f;
static constexpr float kMinAxeIntegrity = 1.f;
static constexpr int32_t kMinLogsForHouse = 5;
static constexpr size_t kTreeCount = 6;

// ---------------------------------------------------------------------------
// Gameplay state
// ---------------------------------------------------------------------------
static GameplayLog g_GameplayLog;

// Forward declarations
static void ImGuiFunc3( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimulationGuid );

// ---------------------------------------------------------------------------
// Register all planner actions
// ---------------------------------------------------------------------------
static void RegisterActions( gie::Planner& planner )
{
	// CutDownTree: cut down a tree if agent has sufficient axe integrity
	planner.addLambdaAction( "CutDownTree",
		[]( gie::EvaluateParams params ) -> bool
		{
			auto axeIntegrityPpt = params.agent.worldContextAgent()->property( "AxeIntegrity" );
			auto simAxeIntegrityPpt = params.simulation.context().property( axeIntegrityPpt->guid() );
			if( !simAxeIntegrityPpt ) return false;
			if( *simAxeIntegrityPpt->getFloat() < kMinAxeIntegrity ) return false;

			auto treeUpTagSet = params.simulation.tagSet( gie::stringHasher( "TreeUp" ) );
			if( !treeUpTagSet || treeUpTagSet->empty() ) return false;
			return true;
		},
		[]( gie::SimulateParams params ) -> bool
		{
			auto axeIntegrityPpt = params.agent.worldContextAgent()->property( "AxeIntegrity" );
			auto simAxeIntegrityPpt = params.simulation.context().property( axeIntegrityPpt->guid() );
			simAxeIntegrityPpt->value = *simAxeIntegrityPpt->getFloat() - 1.f;

			auto treeUpTagSet = params.simulation.tagSet( gie::stringHasher( "TreeUp" ) );
			gie::Guid treeEntityGuid = *treeUpTagSet->cbegin();
			gie::Entity* treeEntity = params.simulation.context().entity( treeEntityGuid );
			auto& simEntityTagRegister = params.simulation.context().entityTagRegister();
			simEntityTagRegister.untag( treeEntity, { gie::stringHasher( "TreeUp" ) } );
			simEntityTagRegister.tag( treeEntity, { gie::stringHasher( "TreeDown" ) } );

			auto cutDownTreeAction = std::make_shared< gie::Action >( gie::stringHasher( "CutDownTree" ) );
			cutDownTreeAction->arguments().add( { gie::stringHasher( "TargetTree" ), treeEntityGuid } );
			params.simulation.actions.emplace_back( cutDownTreeAction );
			return true;
		}
	);

	// Work: earn money when agent has things to buy
	planner.addLambdaAction( "Work",
		[]( gie::EvaluateParams params ) -> bool
		{
			auto moneyPpt = params.agent.worldContextAgent()->property( "Money" );
			auto thingsToBuyPpt = params.agent.worldContextAgent()->property( "ThingsToBuy" );
			const auto simMoneyPpt = params.simulation.context().property( moneyPpt->guid() );
			const auto simThingsToBuyPpt = params.simulation.context().property( thingsToBuyPpt->guid() );

			float cost = 0.f;
			auto thingsToBuyArray = simThingsToBuyPpt->getGuidArray();
			for( gie::Guid thingToBuyGuid : *thingsToBuyArray )
			{
				if( const auto thingToBuyEntity = params.simulation.context().entity( thingToBuyGuid ) )
				{
					auto thingPricePpt = thingToBuyEntity->property( "Price" );
					if( thingPricePpt )
						cost += *thingPricePpt->getFloat();
				}
			}

			if( *simMoneyPpt->getFloat() >= cost ) return false;
			return true;
		},
		[]( gie::SimulateParams params ) -> bool
		{
			auto moneyPpt = params.agent.worldContextAgent()->property( "Money" );
			auto simMoneyPpt = params.simulation.context().property( moneyPpt->guid() );
			simMoneyPpt->value = *simMoneyPpt->getFloat() + kWorkSalary;
			return true;
		}
	);

	// BuyAxe: buy an axe if integrity is low
	planner.addLambdaAction( "BuyAxe",
		[]( gie::EvaluateParams params ) -> bool
		{
			auto axeIntegrityPpt = params.agent.worldContextAgent()->property( "AxeIntegrity" );
			auto simAxeIntegrityPpt = params.simulation.context().property( axeIntegrityPpt->guid() );
			if( !simAxeIntegrityPpt ) return false;
			if( *simAxeIntegrityPpt->getFloat() >= kMinAxeIntegrity ) return false;
			return true;
		},
		[]( gie::SimulateParams params ) -> bool
		{
			auto thingsToBuyPptGuid = params.agent.worldContextAgent()->property( "ThingsToBuy" )->guid();
			auto thingsToBuyPpt = params.simulation.context().property( thingsToBuyPptGuid );
			if( !thingsToBuyPpt ) return false;

			auto axeInfoTagSet = params.agent.worldContextAgent()->world()->context().entityTagRegister().tagSet( gie::stringHasher( "AxeInfo" ) );
			if( !axeInfoTagSet || axeInfoTagSet->empty() ) return false;
			gie::Guid axeInfoEntityGuid = *axeInfoTagSet->cbegin();

			auto moneyPpt = params.agent.worldContextAgent()->property( "Money" );
			auto simMoneyPpt = params.simulation.context().property( moneyPpt->guid() );
			if( !simMoneyPpt ) return false;

			if( *simMoneyPpt->getFloat() >= kAxePrice )
			{
				auto axeIntegrityPpt = params.agent.worldContextAgent()->property( "AxeIntegrity" );
				auto simAxeIntegrityPpt = params.simulation.context().property( axeIntegrityPpt->guid() );
				if( !simAxeIntegrityPpt ) return false;

				simAxeIntegrityPpt->value = kNewAxeIntegrity;
				simMoneyPpt->value = *simMoneyPpt->getFloat() - kAxePrice;

				auto thingsToBuyArray = thingsToBuyPpt->getGuidArray();
				auto newArrayEnd = std::remove( thingsToBuyArray->begin(), thingsToBuyArray->end(), axeInfoEntityGuid );
				if( newArrayEnd != thingsToBuyArray->end() )
					thingsToBuyArray->erase( newArrayEnd, thingsToBuyArray->end() );
				return true;
			}
			else
			{
				auto thingsToBuyArray = thingsToBuyPpt->getGuidArray();
				if( std::find( thingsToBuyArray->begin(), thingsToBuyArray->end(), axeInfoEntityGuid ) != thingsToBuyArray->end() )
					return false;

				thingsToBuyArray->emplace_back( axeInfoEntityGuid );
				auto raiseMoneyNeededAction = std::make_shared< gie::Action >( gie::stringHasher( "NewThingToBuy" ) );
				raiseMoneyNeededAction->arguments().add( { gie::stringHasher( "ThingToBuy" ), axeInfoEntityGuid } );
				params.simulation.actions.emplace_back( raiseMoneyNeededAction );
				return true;
			}
		}
	);

	// BuildHouse: build a house when enough logs are available
	planner.addLambdaAction( "BuildHouse",
		[]( gie::EvaluateParams params ) -> bool
		{
			auto woodHousePpt = params.agent.worldContextAgent()->property( "WoodHouse" );
			auto simWoodHousePpt = params.simulation.context().property( woodHousePpt->guid() );
			if( !simWoodHousePpt ) return false;
			if( *simWoodHousePpt->getBool() ) return false;

			auto treeDownTagSet = params.simulation.tagSet( gie::stringHasher( "TreeDown" ) );
			int32_t availableLogs = static_cast<int32_t>( treeDownTagSet ? treeDownTagSet->size() : 0 );
			return availableLogs >= kMinLogsForHouse;
		},
		[]( gie::SimulateParams params ) -> bool
		{
			auto woodHousePpt = params.agent.worldContextAgent()->property( "WoodHouse" );
			auto simWoodHousePpt = params.simulation.context().property( woodHousePpt->guid() );
			simWoodHousePpt->value = true;

			auto treeDownTagSet = params.simulation.tagSet( gie::stringHasher( "TreeDown" ) );
			if( !treeDownTagSet ) return false;

			auto& entityTagRegister = params.simulation.context().entityTagRegister();
			int32_t logsConsumed = 0;
			for( auto treeGuid : *treeDownTagSet )
			{
				if( logsConsumed >= kMinLogsForHouse ) break;
				auto treeEntity = params.simulation.context().entity( treeGuid );
				if( treeEntity )
				{
					entityTagRegister.untag( treeEntity, { gie::stringHasher( "TreeDown" ) } );
					entityTagRegister.tag( treeEntity, { gie::stringHasher( "TreeUsed" ) } );
					logsConsumed++;
				}
			}
			return true;
		}
	);
}

// ---------------------------------------------------------------------------
// Execute functions — mutate the real world during gameplay
// ---------------------------------------------------------------------------
static std::string ExecuteWork( gie::World& world, gie::Agent* agent )
{
	auto moneyPpt = agent->property( "Money" );
	float before = *moneyPpt->getFloat();
	moneyPpt->value = before + kWorkSalary;
	return "earned $" + std::to_string( static_cast<int>( kWorkSalary ) ) + " (total: $" + std::to_string( static_cast<int>( before + kWorkSalary ) ) + ")";
}

static std::string ExecuteBuyAxe( gie::World& world, gie::Agent* agent )
{
	auto moneyPpt = agent->property( "Money" );
	auto integrityPpt = agent->property( "AxeIntegrity" );
	moneyPpt->value = *moneyPpt->getFloat() - kAxePrice;
	integrityPpt->value = kNewAxeIntegrity;

	auto thingsToBuy = agent->property( "ThingsToBuy" )->getGuidArray();
	thingsToBuy->clear();

	return "bought axe ($" + std::to_string( static_cast<int>( kAxePrice ) ) + ", integrity: " + std::to_string( static_cast<int>( kNewAxeIntegrity ) ) + ")";
}

static std::string ExecuteCutDownTree( gie::World& world, gie::Agent* agent )
{
	auto integrityPpt = agent->property( "AxeIntegrity" );
	integrityPpt->value = *integrityPpt->getFloat() - 1.f;

	auto treeUpSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "TreeUp" ) );
	if( !treeUpSet || treeUpSet->empty() ) return "no trees available";

	gie::Guid treeGuid = *treeUpSet->cbegin();
	auto treeEntity = world.entity( treeGuid );
	world.context().entityTagRegister().untag( treeEntity, { gie::stringHasher( "TreeUp" ) } );
	world.context().entityTagRegister().tag( treeEntity, { gie::stringHasher( "TreeDown" ) } );

	return "cut tree (axe integrity: " + std::to_string( static_cast<int>( *integrityPpt->getFloat() ) ) + ")";
}

static std::string ExecuteBuildHouse( gie::World& world, gie::Agent* agent )
{
	auto treeDownSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "TreeDown" ) );
	if( !treeDownSet ) return "no logs available";

	// Collect GUIDs first to avoid iterator invalidation during untagging
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

static std::string ExecuteAction( gie::World& world, gie::Agent* agent, const std::string& actionName )
{
	if( actionName == "CutDownTree" )     return ExecuteCutDownTree( world, agent );
	else if( actionName == "Work" )       return ExecuteWork( world, agent );
	else if( actionName == "BuyAxe" )     return ExecuteBuyAxe( world, agent );
	else if( actionName == "BuildHouse" ) return ExecuteBuildHouse( world, agent );
	else if( actionName == "NewThingToBuy" ) return ""; // planning-internal signal, skip
	return "";
}

// ---------------------------------------------------------------------------
// Gameplay loop
// ---------------------------------------------------------------------------
static CycleResult RunGameplayCycle( gie::World& world, gie::Agent* agent )
{
	// Check if already reached goal
	auto woodHousePpt = agent->property( "WoodHouse" );
	if( woodHousePpt && *woodHousePpt->getBool() )
	{
		g_GameplayLog.primaryGoalReached = true;
		return CycleResult::GoalReached;
	}

	int cycleNum = static_cast<int>( g_GameplayLog.cycles.size() ) + 1;
	GameplayCycleEntry entry;
	entry.cycle = cycleNum;

	// Plan for primary goal: WoodHouse=true
	gie::Goal primaryGoal{ world };
	primaryGoal.targets.emplace_back( woodHousePpt->guid(), true );

	gie::Planner cyclePlanner{};
	RegisterActions( cyclePlanner );
	cyclePlanner.depthLimitMutator() = 14;
	cyclePlanner.simulate( primaryGoal, *agent );
	cyclePlanner.plan();

	auto& planned = cyclePlanner.planActions();
	if( !planned.empty() )
	{
		entry.goalType = "Primary";
		entry.planFound = true;
		entry.simulationCount = cyclePlanner.simulations().size();
		for( int i = static_cast<int>( planned.size() ) - 1; i >= 0; i-- )
			entry.actionNames.push_back( std::string( planned[i]->name() ) );

		printf( "  Cycle %d: %zu actions —", cycleNum, entry.actionNames.size() );
		for( auto& name : entry.actionNames )
		{
			entry.actionDetails.push_back( ExecuteAction( world, agent, name ) );
			printf( " %s", name.c_str() );
		}
		printf( "\n" );

		g_GameplayLog.cycles.push_back( std::move( entry ) );

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
	g_GameplayLog.cycles.push_back( std::move( entry ) );
	return CycleResult::Stuck;
}

static void RunGameplayLoop( gie::World& world, gie::Agent* agent, gie::Planner& planner )
{
	g_GameplayLog = {};
	g_GameplayLog.started = true;

	const int maxCycles = 10;
	for( int i = 0; i < maxCycles; i++ )
	{
		CycleResult result = RunGameplayCycle( world, agent );
		if( result != CycleResult::Continued )
			break;
	}
}

// ---------------------------------------------------------------------------
// World setup
// ---------------------------------------------------------------------------
static gie::Agent* cutDownTrees_world( ExampleParameters& params )
{
	gie::World& world = params.world;
	params.imGuiDrawFunc = &ImGuiFunc3;
	params.isGameplayExample = true;

	// creating agent
	auto agentEntity = world.createAgent();

	// property telling if agent has a wood house
	agentEntity->createProperty( "WoodHouse", false );

	// Define archetypes (only once per world)
	if( world.archetypes().empty() )
	{
		if( auto* a = world.createArchetype( "Tree" ) )
		{
			a->addTag( "Tree" );
			a->addTag( "TreeUp" );
			a->addTag( "Draw" );
			a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
		}
		if( auto* a = world.createArchetype( "Waypoint" ) )
		{
			a->addTag( "Waypoint" );
			a->addTag( "Draw" );
			a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
			a->addProperty( "Links", gie::Property::GuidVector{} );
		}
	}

	// agent properties
	agentEntity->createProperty( "Money", 0.f );
	agentEntity->createProperty( "ThingsToBuy", gie::Property::GuidVector{} );
	agentEntity->createProperty( "AxeIntegrity", 0.f );

	// axe info entity
	auto axeInfoEntity = world.createEntity();
	axeInfoEntity->createProperty( "Price", kAxePrice );
	world.context().entityTagRegister().tag( axeInfoEntity, { gie::stringHasher( "AxeInfo" ) } );

	// goal: agent must have a wood house
	auto agentWoodHousePpt = agentEntity->property( "WoodHouse" );
	params.goal.targets.emplace_back( agentWoodHousePpt->guid(), true );

	// adding trees to world
	for( size_t i = 0; i < kTreeCount; i++ )
	{
		auto treeEntity = world.createEntity();
		world.context().entityTagRegister().tag( treeEntity, { gie::stringHasher( "Tree" ), gie::stringHasher( "TreeUp" ) } );
	}

	return agentEntity;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int cutDownTrees( ExampleParameters& params )
{
	auto* agent = cutDownTrees_world( params );

	// Register actions on main planner (for visualization Plan! button)
	RegisterActions( params.planner );
	params.planner.depthLimitMutator() = 14;
	params.planner.simulate( params.goal, *agent );

	// Run gameplay loop (deferred in visualization mode — triggered by GUI button)
	if( !params.visualize )
	{
		RunGameplayLoop( params.world, agent, params.planner );
	}

	return 0;
}

// ---------------------------------------------------------------------------
// ImGui panel — gameplay controls and log
// ---------------------------------------------------------------------------
static void ImGuiFunc3( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimGuid )
{
	ImGui::TextUnformatted( "Cut Down Trees - Gameplay Loop (Example 3)" );
	ImGui::Separator();

	// Agent state
	auto agentEnt = world.entity( planner.agent() ? planner.agent()->guid() : gie::NullGuid );
	if( agentEnt )
	{
		ImGui::Text( "Money: $%.0f", *agentEnt->property( "Money" )->getFloat() );
		ImGui::Text( "Axe Integrity: %.0f", *agentEnt->property( "AxeIntegrity" )->getFloat() );
		ImGui::Text( "Wood House: %s", *agentEnt->property( "WoodHouse" )->getBool() ? "YES" : "NO" );
	}

	// Tree counts
	int treeUp = 0, treeDown = 0, treeUsed = 0;
	if( auto s = world.context().entityTagRegister().tagSet( gie::stringHasher( "TreeUp" ) ) )
		treeUp = static_cast<int>( s->size() );
	if( auto s = world.context().entityTagRegister().tagSet( gie::stringHasher( "TreeDown" ) ) )
		treeDown = static_cast<int>( s->size() );
	if( auto s = world.context().entityTagRegister().tagSet( gie::stringHasher( "TreeUsed" ) ) )
		treeUsed = static_cast<int>( s->size() );
	ImGui::Text( "Trees: %d up, %d down (logs), %d used", treeUp, treeDown, treeUsed );

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
				g_GameplayLog.started = true;

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
	{
		ImGui::TextColored( ImVec4( 0.2f, 1.0f, 0.2f, 1.0f ), "Goal Reached: Wood House Built!" );
	}
	else if( finished )
	{
		ImGui::TextColored( ImVec4( 1.0f, 0.3f, 0.3f, 1.0f ), "Stuck: No plan found" );
	}

	// Gameplay log
	if( !g_GameplayLog.cycles.empty() )
	{
		ImGui::Separator();
		if( ImGui::CollapsingHeader( "Gameplay Log", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			for( auto& cycle : g_GameplayLog.cycles )
			{
				bool open = ImGui::TreeNode( reinterpret_cast<void*>( static_cast<intptr_t>( cycle.cycle ) ),
					"Cycle %d [%s] (%zu actions, %zu sims)",
					cycle.cycle, cycle.goalType.c_str(),
					cycle.actionNames.size(), cycle.simulationCount );
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

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
int cutDownTreesValidateResult( std::string& failMsg )
{
	gie::World world{};
	gie::Planner planner{};
	gie::Goal goal{ world };
	ExampleParameters params{ world, planner, goal };

	VALIDATE( cutDownTrees( params ) == 0, "cutDownTrees() setup failed" );

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

	// Check tree state: at least kMinLogsForHouse trees consumed
	auto treeUsedSet = world.context().entityTagRegister().tagSet( gie::stringHasher( "TreeUsed" ) );
	int usedCount = treeUsedSet ? static_cast<int>( treeUsedSet->size() ) : 0;
	VALIDATE( usedCount >= kMinLogsForHouse, "should have consumed enough trees for the house" );

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
