#ifndef GOAPIE_LIB
#define GOAPIE_LIB

// Core includes
#include "common.h"
#include "definitions.h"

// Data structures
#include "property.h"
#include "arguments.h"

// Entity system
#include "entity.h"
#include "blackboard.h"
#include "agent.h"
#include "archetype.h"
#include "world.h"

// Simulation system first (defines DebugMessages)
#include "simulation.h"

// Planning system (depends on simulation)
#include "goal.h"
#include "action.h"
#include "planner.h"

// Inline implementations (header-only)
#include "archetype.inl"

#endif