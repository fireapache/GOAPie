// GOAPieAsyncPlanner.cpp

#include "GOAPieAsyncPlanner.h"

FGOAPieAsyncPlanner::~FGOAPieAsyncPlanner()
{
	CancelAll();
}

FGOAPiePlanHandle FGOAPieAsyncPlanner::RequestPlan(
	gie::Planner& Planner,
	FOnPlanningComplete OnComplete,
	bool bUseHeuristic)
{
	FScopeLock Lock(&PlansMutex);

	FGOAPiePlanHandle Handle;
	Handle.Id = NextHandleId++;

	auto Task = MakeShared<FGOAPiePlannerTask>();
	Task->SetupPlanning(Planner, bUseHeuristic);
	Task->OnPlanningComplete = OnComplete;

	FString ThreadName = FString::Printf(TEXT("GOAPiePlan_%u"), Handle.Id);
	Task->StartAsync(ThreadName);

	FPlanEntry Entry;
	Entry.Handle = Handle;
	Entry.Task = Task;
	ActivePlans.Add(MoveTemp(Entry));

	return Handle;
}

void FGOAPieAsyncPlanner::CancelPlan(FGOAPiePlanHandle Handle)
{
	FScopeLock Lock(&PlansMutex);

	for (int32 i = ActivePlans.Num() - 1; i >= 0; --i)
	{
		if (ActivePlans[i].Handle == Handle)
		{
			if (ActivePlans[i].Task)
			{
				ActivePlans[i].Task->RequestCancel();
			}
			ActivePlans.RemoveAt(i);
			break;
		}
	}
}

void FGOAPieAsyncPlanner::CancelAll()
{
	FScopeLock Lock(&PlansMutex);

	for (auto& Entry : ActivePlans)
	{
		if (Entry.Task)
		{
			Entry.Task->RequestCancel();
		}
	}
	ActivePlans.Empty();
}

bool FGOAPieAsyncPlanner::IsPlanInProgress(FGOAPiePlanHandle Handle) const
{
	FScopeLock Lock(&PlansMutex);

	for (const auto& Entry : ActivePlans)
	{
		if (Entry.Handle == Handle)
		{
			return Entry.Task && !Entry.Task->IsComplete();
		}
	}
	return false;
}

int32 FGOAPieAsyncPlanner::GetActivePlanCount() const
{
	FScopeLock Lock(&PlansMutex);
	return ActivePlans.Num();
}

void FGOAPieAsyncPlanner::Tick()
{
	FScopeLock Lock(&PlansMutex);

	// Remove completed tasks
	for (int32 i = ActivePlans.Num() - 1; i >= 0; --i)
	{
		if (ActivePlans[i].Task && ActivePlans[i].Task->IsComplete())
		{
			ActivePlans.RemoveAt(i);
		}
	}
}
