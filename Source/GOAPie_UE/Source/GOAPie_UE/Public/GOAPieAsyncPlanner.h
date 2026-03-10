// GOAPieAsyncPlanner.h
// High-level async planning manager for Unreal Engine game code.
//
// Wraps the GOAPie planner with Unreal's task threading system to support:
//   1. Fire-and-forget async planning with game-thread callbacks
//   2. Multiple concurrent plan requests (queued or parallel)
//   3. Cancellation of in-flight planning operations
//
// This is game-code-level infrastructure — the GOAPie core library remains
// untouched and fully synchronous. All threading is handled here using
// Unreal's FRunnable/FRunnableThread.
//
// Example usage from an AI controller or component:
//
//   // Setup world and planner normally
//   gie::World World;
//   gie::Planner Planner;
//   gie::Goal Goal(World);
//   // ... register actions, create entities, set targets ...
//   Planner.simulate(Goal, Agent);
//
//   // Request async planning
//   FGOAPieAsyncPlanner AsyncPlanner;
//   FGOAPiePlanHandle Handle = AsyncPlanner.RequestPlan(
//       Planner,
//       FOnPlanningComplete::CreateLambda([](const FGOAPiePlanResult& Result)
//       {
//           if (Result.bSuccess)
//           {
//               for (const FString& Action : Result.ActionNames)
//               {
//                   UE_LOG(LogTemp, Log, TEXT("Plan action: %s"), *Action);
//               }
//           }
//       }),
//       true  // bUseHeuristic
//   );
//
//   // Optionally cancel
//   AsyncPlanner.CancelPlan(Handle);
//
//   // Cleanup: cancel all and wait
//   AsyncPlanner.CancelAll();

#pragma once

#include "CoreMinimal.h"
#include "GOAPiePlannerTask.h"

// Opaque handle to a pending plan request
struct FGOAPiePlanHandle
{
	uint32 Id = 0;
	bool IsValid() const { return Id != 0; }

	bool operator==(const FGOAPiePlanHandle& Other) const { return Id == Other.Id; }
	bool operator!=(const FGOAPiePlanHandle& Other) const { return Id != Other.Id; }
};

/**
 * Manages async planning requests.
 *
 * Each RequestPlan() call creates an FGOAPiePlannerTask that runs on a
 * background thread. The completion delegate is always fired on the game thread.
 *
 * Thread safety of the GOAPie data structures is the caller's responsibility.
 * The simplest pattern: clone the World before requesting a plan, so the
 * background thread operates on an independent copy.
 *
 * Lifecycle: create as a member of your AI controller, component, or subsystem.
 * Call CancelAll() in the destructor or when the owning object is destroyed.
 */
class GOAPIE_UE_API FGOAPieAsyncPlanner
{
public:
	FGOAPieAsyncPlanner() = default;
	~FGOAPieAsyncPlanner();

	// Non-copyable
	FGOAPieAsyncPlanner(const FGOAPieAsyncPlanner&) = delete;
	FGOAPieAsyncPlanner& operator=(const FGOAPieAsyncPlanner&) = delete;

	/**
	 * Submit a planning request to run on a background thread.
	 *
	 * @param Planner       The GOAPie planner (must have simulate() called already).
	 *                      Must remain alive until the task completes or is cancelled.
	 * @param OnComplete    Delegate fired on game thread when planning finishes.
	 * @param bUseHeuristic Whether to use A* (true) or BFS (false).
	 * @return Handle to the pending request, usable for cancellation.
	 */
	FGOAPiePlanHandle RequestPlan(
		gie::Planner& Planner,
		FOnPlanningComplete OnComplete,
		bool bUseHeuristic = true);

	/**
	 * Cancel a specific pending plan request.
	 * If the task hasn't started yet, it is removed. If running, it's signaled to stop.
	 */
	void CancelPlan(FGOAPiePlanHandle Handle);

	/** Cancel all pending and running plan requests. */
	void CancelAll();

	/** Check if a specific plan request is still in progress. */
	bool IsPlanInProgress(FGOAPiePlanHandle Handle) const;

	/** Get the number of currently active (running or pending) plan requests. */
	int32 GetActivePlanCount() const;

	/**
	 * Tick the async planner to clean up completed tasks.
	 * Call this from your Tick() if you want automatic cleanup,
	 * or let tasks self-clean via their completion delegates.
	 */
	void Tick();

private:
	struct FPlanEntry
	{
		FGOAPiePlanHandle Handle;
		TSharedPtr<FGOAPiePlannerTask> Task;
	};

	TArray<FPlanEntry> ActivePlans;
	uint32 NextHandleId = 1;
	mutable FCriticalSection PlansMutex;
};
