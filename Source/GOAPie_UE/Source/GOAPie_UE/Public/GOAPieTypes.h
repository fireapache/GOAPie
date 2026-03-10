// GOAPieTypes.h
// Convenience header that re-exports GOAPie core types with UE-friendly wrappers.

#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include <goapie.h>
THIRD_PARTY_INCLUDES_END

// Re-export commonly used types into a UE-friendly namespace
namespace GOAPie
{
	using World = gie::World;
	using Planner = gie::Planner;
	using Goal = gie::Goal;
	using Agent = gie::Agent;
	using Entity = gie::Entity;
	using Property = gie::Property;
	using Blackboard = gie::Blackboard;
	using Simulation = gie::Simulation;
	using Action = gie::Action;
	using ActionSetEntry = gie::ActionSetEntry;
	using ActionSimulator = gie::ActionSimulator;
	using Guid = gie::Guid;

	using EvaluateFunc = gie::EvaluateFunc;
	using SimulateFunc = gie::SimulateFunc;
	using HeuristicFunc = gie::HeuristicFunc;
	using EvaluateSimulationParams = gie::EvaluateSimulationParams;
	using SimulateSimulationParams = gie::SimulateSimulationParams;
	using CalculateHeuristicParams = gie::CalculateHeuristicParams;

	static constexpr Guid NullGuid = gie::NullGuid;
}
