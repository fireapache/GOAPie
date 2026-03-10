// GOAPiePlannerTask.cpp

#include "GOAPiePlannerTask.h"
#include "Async/Async.h"

FGOAPiePlannerTask::~FGOAPiePlannerTask()
{
	// Ensure the thread is cleaned up
	if (Thread)
	{
		Thread->Kill(true);
		delete Thread;
		Thread = nullptr;
	}
}

void FGOAPiePlannerTask::SetupPlanning(gie::Planner& InPlanner, bool bInUseHeuristic)
{
	check(!bRunning);
	Planner = &InPlanner;
	bUseHeuristic = bInUseHeuristic;
	bComplete->Reset();
	bCancelRequested = false;
	Result = FGOAPiePlanResult();
}

void FGOAPiePlannerTask::StartAsync(const FString& ThreadName)
{
	check(Planner != nullptr);
	check(!bRunning);

	bRunning = true;

	// Create a system thread for planning
	Thread = FRunnableThread::Create(
		this,
		*ThreadName,
		0,           // stack size (0 = default)
		TPri_Normal  // priority
	);

	if (!Thread)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Failed to create planning thread");
		bRunning = false;
		bComplete->Set(1);
		BroadcastResult();
	}
}

FGOAPiePlanResult FGOAPiePlannerTask::RunSynchronous()
{
	check(Planner != nullptr);
	check(!bRunning);

	bRunning = true;
	ExecutePlanning();
	bRunning = false;

	return Result;
}

void FGOAPiePlannerTask::RequestCancel()
{
	bCancelRequested = true;
}

uint32 FGOAPiePlannerTask::Run()
{
	ExecutePlanning();
	bRunning = false;

	// Fire delegate on game thread
	BroadcastResult();

	return 0;
}

void FGOAPiePlannerTask::Stop()
{
	bCancelRequested = true;
}

void FGOAPiePlannerTask::ExecutePlanning()
{
	if (!Planner)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Planner is null");
		bComplete->Set(1);
		return;
	}

	const double StartTime = FPlatformTime::Seconds();

	// Run the planner (this is the heavy work)
	Planner->plan(bUseHeuristic);

	const double EndTime = FPlatformTime::Seconds();
	Result.PlanningDurationSeconds = EndTime - StartTime;

	// Extract results
	const auto& PlannedActions = Planner->planActions();
	Result.SimulationCount = static_cast<int32>(Planner->simulations().size());

	if (PlannedActions.empty())
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("No plan found (goal not reachable within depth limit)");
	}
	else
	{
		Result.bSuccess = true;
		Result.ActionNames.Reserve(PlannedActions.size());

		// planActions are stored leaf-to-root, iterate in reverse for execution order
		for (auto It = PlannedActions.rbegin(); It != PlannedActions.rend(); ++It)
		{
			if (*It)
			{
				auto Name = (*It)->name();
				Result.ActionNames.Add(FString(Name.size(), Name.data()));
			}
		}
	}

	bComplete->Set(1);
}

void FGOAPiePlannerTask::BroadcastResult()
{
	if (OnPlanningComplete.IsBound())
	{
		// Ensure delegate fires on the game thread
		TWeakPtr<FGOAPiePlannerTask> WeakThis = AsShared();
		FGOAPiePlanResult ResultCopy = Result;
		FOnPlanningComplete DelegateCopy = OnPlanningComplete;

		AsyncTask(ENamedThreads::GameThread, [WeakThis, ResultCopy = MoveTemp(ResultCopy), DelegateCopy]()
		{
			if (WeakThis.IsValid())
			{
				DelegateCopy.ExecuteIfBound(ResultCopy);
			}
		});
	}
}
