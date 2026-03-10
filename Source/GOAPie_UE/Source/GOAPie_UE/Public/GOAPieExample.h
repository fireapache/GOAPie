// GOAPieExample.h
// Example code demonstrating how to use GOAPie with threaded planning in UE.
// This is NOT compiled into the plugin — it serves as documentation and reference.
//
// Copy and adapt these patterns into your AI controllers, components, or subsystems.

#pragma once

#if 0 // ===== EXAMPLE CODE — NOT COMPILED =====

#include "GOAPie.h"
#include "GameFramework/Actor.h"

// ---------------------------------------------------------------------------
// EXAMPLE 1: Simple async planning from an AI Controller
// ---------------------------------------------------------------------------
//
// This shows the most common pattern: an AI agent requests a plan on a
// background thread and processes the result when it arrives on the game thread.

class AMyAIController : public AAIController
{
	// GOAPie data — owned by this controller
	TUniquePtr<GOAPie::World> PlanWorld;
	TUniquePtr<GOAPie::Planner> PlanPlanner;
	TUniquePtr<GOAPie::Goal> PlanGoal;
	GOAPie::Agent* PlanAgent = nullptr;

	// Async planner manages background thread lifecycle
	FGOAPieAsyncPlanner AsyncPlanner;

	// Handle to the current in-flight plan request
	FGOAPiePlanHandle CurrentPlanHandle;

	// Current action queue from the last successful plan
	TArray<FString> ActionQueue;
	int32 CurrentActionIndex = 0;

	void SetupGOAPWorld()
	{
		PlanWorld = MakeUnique<GOAPie::World>();
		PlanPlanner = MakeUnique<GOAPie::Planner>();
		PlanGoal = MakeUnique<GOAPie::Goal>(*PlanWorld);

		// Create agent
		PlanAgent = PlanWorld->createAgent("AIAgent");
		PlanAgent->createProperty("Location", glm::vec3(0.f));
		PlanAgent->createProperty("HasWeapon", false);
		PlanAgent->createProperty("TargetEliminated", false);

		// Register actions (these run on the background thread during planning)
		PlanPlanner->addLambdaAction("FindWeapon",
			[](gie::EvaluateSimulationParams params) -> bool
			{
				auto agent = params.simulation.context().entity(params.agent.guid());
				auto hasWeapon = agent->property("HasWeapon");
				return hasWeapon && !*hasWeapon->getBool();
			},
			[](gie::SimulateSimulationParams params) -> bool
			{
				auto agent = params.simulation.context().entity(params.agent.guid());
				agent->property("HasWeapon")->value = true;
				params.simulation.cost = 5.f;
				return true;
			}
		);

		PlanPlanner->addLambdaAction("EliminateTarget",
			[](gie::EvaluateSimulationParams params) -> bool
			{
				auto agent = params.simulation.context().entity(params.agent.guid());
				auto hasWeapon = agent->property("HasWeapon");
				return hasWeapon && *hasWeapon->getBool();
			},
			[](gie::SimulateSimulationParams params) -> bool
			{
				auto agent = params.simulation.context().entity(params.agent.guid());
				agent->property("TargetEliminated")->value = true;
				params.simulation.cost = 3.f;
				return true;
			}
		);

		// Set goal
		auto targetProp = PlanAgent->property("TargetEliminated");
		PlanGoal->targets.emplace_back(targetProp->guid(), true);

		// Initialize planner with goal and agent
		PlanPlanner->simulate(*PlanGoal, *PlanAgent);
	}

	void RequestNewPlan()
	{
		// Cancel any in-flight plan
		if (CurrentPlanHandle.IsValid())
		{
			AsyncPlanner.CancelPlan(CurrentPlanHandle);
		}

		// Re-initialize planner state (required before each plan() call)
		PlanPlanner->simulate(*PlanGoal, *PlanAgent);

		// Fire off async planning
		CurrentPlanHandle = AsyncPlanner.RequestPlan(
			*PlanPlanner,
			FOnPlanningComplete::CreateRaw(this, &AMyAIController::OnPlanReady),
			true // A* heuristic
		);

		UE_LOG(LogTemp, Log, TEXT("GOAPie: Planning requested (handle=%u)"), CurrentPlanHandle.Id);
	}

	void OnPlanReady(const FGOAPiePlanResult& Result)
	{
		// This runs on the game thread — safe to modify game state
		CurrentPlanHandle = FGOAPiePlanHandle{}; // clear handle

		if (Result.bSuccess)
		{
			UE_LOG(LogTemp, Log, TEXT("GOAPie: Plan found (%d actions, %.1fms, %d sims)"),
				Result.ActionNames.Num(),
				Result.PlanningDurationSeconds * 1000.0,
				Result.SimulationCount);

			ActionQueue = Result.ActionNames;
			CurrentActionIndex = 0;
			ExecuteNextAction();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("GOAPie: No plan found — %s"), *Result.ErrorMessage);
			// Fallback behavior (idle, patrol, etc.)
		}
	}

	void ExecuteNextAction()
	{
		if (CurrentActionIndex >= ActionQueue.Num())
		{
			// Plan complete — request a new one or go idle
			return;
		}

		const FString& ActionName = ActionQueue[CurrentActionIndex];
		CurrentActionIndex++;

		// Dispatch to your action execution system
		UE_LOG(LogTemp, Log, TEXT("GOAPie: Executing action: %s"), *ActionName);

		// ... execute the action, call ExecuteNextAction() when done ...
	}

	virtual void BeginPlay() override
	{
		Super::BeginPlay();
		SetupGOAPWorld();
		RequestNewPlan();
	}

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override
	{
		// Clean up async tasks before destroying GOAPie data
		AsyncPlanner.CancelAll();
		Super::EndPlay(EndPlayReason);
	}
};


// ---------------------------------------------------------------------------
// EXAMPLE 2: Synchronous planning (for simple cases or testing)
// ---------------------------------------------------------------------------
//
// Sometimes you just want a plan immediately, without threading overhead.
// FGOAPiePlannerTask::RunSynchronous() runs on the calling thread.

void PlanSynchronousExample()
{
	GOAPie::World World;
	GOAPie::Planner Planner;
	GOAPie::Goal Goal(World);

	auto* Agent = World.createAgent("TestAgent");
	Agent->createProperty("Done", false);

	Planner.addLambdaAction("DoThing",
		[](gie::EvaluateSimulationParams) { return true; },
		[](gie::SimulateSimulationParams params) {
			auto agent = params.simulation.context().entity(params.agent.guid());
			agent->property("Done")->value = true;
			return true;
		}
	);

	Goal.targets.emplace_back(Agent->property("Done")->guid(), true);
	Planner.simulate(Goal, *Agent);

	auto Task = MakeShared<FGOAPiePlannerTask>();
	Task->SetupPlanning(Planner, true);
	FGOAPiePlanResult Result = Task->RunSynchronous();

	if (Result.bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("Synchronous plan: %d actions in %.3fms"),
			Result.ActionNames.Num(),
			Result.PlanningDurationSeconds * 1000.0);
	}
}


// ---------------------------------------------------------------------------
// EXAMPLE 3: Multiple agents planning concurrently
// ---------------------------------------------------------------------------
//
// Each agent gets its own World clone and Planner, so they can plan in parallel
// without data races.

class AMyAIManager : public AActor
{
	struct FAgentPlanData
	{
		TUniquePtr<GOAPie::World> World;
		TUniquePtr<GOAPie::Planner> Planner;
		TUniquePtr<GOAPie::Goal> Goal;
		GOAPie::Agent* Agent = nullptr;
		FGOAPiePlanHandle Handle;
	};

	TMap<int32, FAgentPlanData> AgentData;
	FGOAPieAsyncPlanner AsyncPlanner;

	void RequestPlanForAgent(int32 AgentId)
	{
		auto* Data = AgentData.Find(AgentId);
		if (!Data) return;

		// Cancel existing plan for this agent
		if (Data->Handle.IsValid())
		{
			AsyncPlanner.CancelPlan(Data->Handle);
		}

		Data->Planner->simulate(*Data->Goal, *Data->Agent);

		Data->Handle = AsyncPlanner.RequestPlan(
			*Data->Planner,
			FOnPlanningComplete::CreateLambda([this, AgentId](const FGOAPiePlanResult& Result)
			{
				UE_LOG(LogTemp, Log, TEXT("Agent %d plan: %s (%d actions)"),
					AgentId,
					Result.bSuccess ? TEXT("success") : TEXT("failed"),
					Result.ActionNames.Num());
			}),
			true
		);
	}

	virtual void Tick(float DeltaTime) override
	{
		Super::Tick(DeltaTime);
		// Clean up completed tasks periodically
		AsyncPlanner.Tick();
	}

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override
	{
		AsyncPlanner.CancelAll();
		Super::EndPlay(EndPlayReason);
	}
};


// ---------------------------------------------------------------------------
// EXAMPLE 4: Plan-Execute-Replan loop (like Example 7's gameplay cycle)
// ---------------------------------------------------------------------------
//
// For scenarios with partial observability or dynamic worlds, you may need
// to plan, execute, observe, and replan repeatedly.

class AMyExplorerAI : public AAIController
{
	TUniquePtr<GOAPie::World> World;
	GOAPie::Agent* Agent = nullptr;
	FGOAPieAsyncPlanner AsyncPlanner;
	FGOAPiePlanHandle CurrentHandle;
	TArray<FString> CurrentPlan;
	int32 PlanStep = 0;
	bool bReplanning = false;

	void RequestReplan()
	{
		if (bReplanning) return;
		bReplanning = true;

		auto Planner = MakeUnique<GOAPie::Planner>();
		auto Goal = MakeUnique<GOAPie::Goal>(*World);

		// Register actions, set goal targets...
		// ...

		Planner->simulate(*Goal, *Agent);

		// Store planner/goal so they outlive the async task
		auto* PlannerPtr = Planner.Get();
		// NOTE: must ensure Planner stays alive — move into a member or shared ptr

		CurrentHandle = AsyncPlanner.RequestPlan(
			*PlannerPtr,
			FOnPlanningComplete::CreateLambda([this](const FGOAPiePlanResult& Result)
			{
				bReplanning = false;
				if (Result.bSuccess)
				{
					CurrentPlan = Result.ActionNames;
					PlanStep = 0;
					ExecuteCurrentStep();
				}
				else
				{
					// Try exploration goal instead...
					RequestExplorationPlan();
				}
			}),
			true
		);
	}

	void ExecuteCurrentStep()
	{
		if (PlanStep >= CurrentPlan.Num())
		{
			// Plan exhausted — replan with updated world knowledge
			RequestReplan();
			return;
		}

		// Execute action, advance PlanStep when done, check if replan needed
	}

	void RequestExplorationPlan()
	{
		// Similar to RequestReplan but with an exploration-focused goal
	}
};

#endif // ===== END EXAMPLE CODE =====
