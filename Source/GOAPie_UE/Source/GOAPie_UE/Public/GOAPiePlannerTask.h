// GOAPiePlannerTask.h
// FRunnable-based task that executes GOAPie planning on a background thread.
//
// Usage from game code:
//
//   // 1. Set up your world, planner, and goal as usual
//   auto Task = MakeShared<FGOAPiePlannerTask>();
//   Task->SetupPlanning(MyPlanner, MyGoal, MyAgent, bUseHeuristic);
//
//   // 2. Bind completion delegate (called on game thread)
//   Task->OnPlanningComplete.BindLambda([](const FGOAPiePlanResult& Result)
//   {
//       if (Result.bSuccess) { /* use Result.ActionNames */ }
//   });
//
//   // 3. Start the task
//   Task->StartAsync();
//
//   // 4. Poll IsComplete() or wait for the delegate

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Async/Async.h"
#include "Misc/ScopeLock.h"

THIRD_PARTY_INCLUDES_START
#include <goapie.h>
THIRD_PARTY_INCLUDES_END

// Result of a planning operation
struct FGOAPiePlanResult
{
	// Whether a valid plan was found
	bool bSuccess = false;

	// Ordered action names from the plan (first to last)
	TArray<FString> ActionNames;

	// Number of simulation nodes explored
	int32 SimulationCount = 0;

	// Time spent planning (seconds)
	double PlanningDurationSeconds = 0.0;

	// Error message if planning failed
	FString ErrorMessage;
};

// Delegate fired when planning completes (always on game thread)
DECLARE_DELEGATE_OneParam(FOnPlanningComplete, const FGOAPiePlanResult&);

// Delegate fired when an async action requests deferred work.
// The action returns false from evaluate/simulate, and the planner pauses.
// Game code resolves the request, then calls ResumePlanning().
DECLARE_DELEGATE_OneParam(FOnAsyncActionRequested, const FString& /*RequestDescription*/);

/**
 * FRunnable that executes GOAPie planning on a background thread.
 *
 * The planner, world, goal, and agent must remain alive for the duration
 * of the planning task. The caller is responsible for lifetime management.
 *
 * Thread safety: the GOAPie core is not thread-safe. Ensure that no other
 * code reads or mutates the World/Planner/Goal/Agent while planning is active.
 * A typical pattern is to clone the World before planning, or to schedule
 * planning during a phase where the game world is not being mutated.
 */
class GOAPIE_UE_API FGOAPiePlannerTask : public FRunnable, public TSharedFromThis<FGOAPiePlannerTask>
{
public:
	FGOAPiePlannerTask() = default;
	virtual ~FGOAPiePlannerTask();

	// -----------------------------------------------------------------------
	// Setup
	// -----------------------------------------------------------------------

	/**
	 * Configure the planning parameters. Must be called before StartAsync().
	 * The Planner must already have actions registered and simulate() called.
	 */
	void SetupPlanning(gie::Planner& InPlanner, bool bUseHeuristic = true);

	// -----------------------------------------------------------------------
	// Execution
	// -----------------------------------------------------------------------

	/** Start planning on a background thread. Non-blocking. */
	void StartAsync(const FString& ThreadName = TEXT("GOAPiePlanner"));

	/** Run planning synchronously on the calling thread. Blocking. */
	FGOAPiePlanResult RunSynchronous();

	/** Cancel a running planning task. The task will stop at the next safe point. */
	void RequestCancel();

	/** Check if planning has completed (success or failure). */
	bool IsComplete() const { return bComplete.IsValid() && bComplete->GetValue() != 0; }

	/** Check if the task is currently running. */
	bool IsRunning() const { return bRunning; }

	/** Get the result (only valid after IsComplete() returns true). */
	const FGOAPiePlanResult& GetResult() const { return Result; }

	// -----------------------------------------------------------------------
	// Delegates
	// -----------------------------------------------------------------------

	/** Fired on the game thread when planning completes. */
	FOnPlanningComplete OnPlanningComplete;

	// -----------------------------------------------------------------------
	// FRunnable interface
	// -----------------------------------------------------------------------

	virtual bool Init() override { return true; }
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	void ExecutePlanning();
	void BroadcastResult();

	gie::Planner* Planner = nullptr;
	bool bUseHeuristic = true;

	FGOAPiePlanResult Result;

	FRunnableThread* Thread = nullptr;
	TSharedPtr<FThreadSafeCounter> bComplete = MakeShared<FThreadSafeCounter>();
	FThreadSafeBool bCancelRequested = false;
	bool bRunning = false;
};
