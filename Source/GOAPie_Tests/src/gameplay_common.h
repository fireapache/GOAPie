#pragma once

// Shared gameplay loop types and rendering helpers — used by examples 3, 5, 6, and 7.

#include <string>
#include <vector>
#include <memory>

#include <glm/glm.hpp>
#include <glad/glad.h>

// Forward declarations
namespace gie { class Planner; }

// ---------------------------------------------------------------------------
// Cycle result enum
// ---------------------------------------------------------------------------
enum class CycleResult { Continued, GoalReached, Stuck };

// ---------------------------------------------------------------------------
// Gameplay cycle entry — one planning cycle's record
// ---------------------------------------------------------------------------
struct GameplayCycleEntry
{
	int cycle{ 0 };
	std::string goalType;          // "Primary", "Explore", "Survival", etc.
	bool planFound{ false };
	std::vector<std::string> actionNames;   // execution order (first to last)
	std::vector<std::string> actionDetails; // detailed description per action
	glm::vec3 agentPosBefore{ 0.f };
	glm::vec3 agentPosAfter{ 0.f };
	int knownCountBefore{ 0 };
	int knownCountAfter{ 0 };
	std::vector<std::string> inventoryAfter;
	size_t simulationCount{ 0 };
	std::unique_ptr<gie::Planner> plannerPtr; // preserved planner with full simulation tree
};

// ---------------------------------------------------------------------------
// Gameplay log — aggregates all cycles for an example run
// ---------------------------------------------------------------------------
struct GameplayLog
{
	std::vector<GameplayCycleEntry> cycles;
	std::vector<glm::vec3> agentTrail; // breadcrumb trail of positions
	bool primaryGoalReached{ false };
	int selectedCycle{ -1 }; // for UI highlight
	bool started{ false };   // has the gameplay loop been executed?
};

// ---------------------------------------------------------------------------
// GL cycle-path rendering helpers
// ---------------------------------------------------------------------------

// Set line style for a cycle's movement path.
// When a specific cycle IS selected (highlightCycle >= 0), callers should skip
// non-matching cycles and pass isSelected=true for the matched cycle.
// When no cycle is selected, all cycles are drawn with age-based styling:
//   - Latest cycle:  bright cyan, width 3
//   - Older cycles:  faded warm, width 1.5, alpha fades from 0.15 to 0.5
inline void SetCyclePathStyle( size_t cycleIndex, size_t totalCycles, bool isSelected )
{
	if( isSelected )
	{
		glLineWidth( 3.0f );
		glColor4f( 1.0f, 0.84f, 0.0f, 1.0f );
		return;
	}

	const bool isLatest = ( totalCycles > 0 && cycleIndex + 1 == totalCycles );
	if( isLatest )
	{
		glLineWidth( 3.0f );
		glColor4f( 0.4f, 0.9f, 1.0f, 0.9f );
	}
	else
	{
		glLineWidth( 1.5f );
		float t = ( totalCycles > 2 )
			? static_cast<float>( cycleIndex ) / static_cast<float>( totalCycles - 2 )
			: 0.f;
		float alpha = 0.15f + t * 0.35f;
		glColor4f( 0.6f, 0.5f, 0.35f, alpha );
	}
}

// Point size for cycle waypoint dots.
inline float CyclePathPointSize( size_t cycleIndex, size_t totalCycles )
{
	return ( totalCycles > 0 && cycleIndex + 1 == totalCycles ) ? 6.0f : 4.0f;
}

// Set color for cycle waypoint dots (matches the cycle's line color).
inline void SetCyclePathDotColor( size_t cycleIndex, size_t totalCycles, bool isSelected )
{
	if( isSelected )
	{
		glColor4f( 1.0f, 0.84f, 0.0f, 1.0f );
		return;
	}

	const bool isLatest = ( totalCycles > 0 && cycleIndex + 1 == totalCycles );
	if( isLatest )
	{
		glColor4f( 0.4f, 0.9f, 1.0f, 0.9f );
	}
	else
	{
		float t = ( totalCycles > 2 )
			? static_cast<float>( cycleIndex ) / static_cast<float>( totalCycles - 2 )
			: 0.f;
		float alpha = 0.15f + t * 0.35f;
		glColor4f( 0.6f, 0.5f, 0.35f, alpha );
	}
}
