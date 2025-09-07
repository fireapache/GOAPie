#pragma once

// Lightweight header-only scaffolding for Lua-backed actions.
// NOTE:
// - This header provides a safe, compileable placeholder implementation so the
//   codebase can be extended incrementally. The actual Lua binding is guarded
//   and can be implemented later (or enabled in the Tests target).
// - It intentionally keeps dependencies minimal to avoid forcing a Lua dependency
//   into every consumer of the GOAPie headers.
//
// Primary types:
// - LuaSandbox        : runtime wrapper (stub here) that would host lua_State.
// - LuaActionSimulator: ActionSimulator that delegates evaluate/simulate/heuristic
//                       to user-provided Lua snippets (stored as strings).
// - LuaActionSetEntry : ActionSetEntry factory producing LuaActionSimulator instances.
// - helpers           : small utility to append Lua entries into a Planner.
//
// TODO: Replace stubs with real Lua 5.4 marshalling once the test-app links Lua.

#include <string>
#include <vector>
#include <memory>

#include "action.h"
#include "arguments.h"
#include "simulation.h"
#include "goal.h"
#include "planner.h"

namespace gie
{
// Forward-declare any heavy Lua types behind a macro so builds without Lua still work.
#ifndef GIE_WITH_LUA
#define GIE_WITH_LUA 0
#endif

// Minimal runtime wrapper (no-op when GIE_WITH_LUA == 0).
class LuaSandbox
{
public:
    LuaSandbox() = default;
    ~LuaSandbox() = default;

    // Load a chunk (source) into the sandbox. Returns true on success.
    // When real Lua is enabled this should compile the chunk and keep a reference.
    bool loadChunk( const std::string& /*name*/, const std::string& /*source*/ )
    {
        // stub success so editors/tests can register scripts without failure
        return true;
    }

    // Execute an evaluation chunk with given context. The real implementation will
    // marshal EvaluateSimulationParams into Lua and return a boolean.
    bool executeEvaluate( const std::string& /*chunkName*/, const EvaluateSimulationParams& params ) const
    {
        (void)params;
        // Default behavior: allow action (true). Real logic should query Lua.
        return true;
    }

    // Execute the simulate chunk. Returns true on success.
    bool executeSimulate( const std::string& /*chunkName*/, SimulateSimulationParams& params ) const
    {
        (void)params;
        // Default behavior: succeed simulation so action can be used in planner.
        return true;
    }

    // Execute heuristic chunk; should update simulation.heuristic in real impl.
    void executeHeuristic( const std::string& /*chunkName*/, CalculateHeuristicParams& params ) const
    {
        (void)params;
        // No-op stub.
    }
};

// Lua-backed ActionSimulator. Stores the source (or chunk names) and calls
// into LuaSandbox when available. Header-only stub returns permissive defaults.
class LuaActionSimulator : public ActionSimulator
{
public:
    LuaActionSimulator( const std::string& name,
                        const std::string& evaluateChunk,
                        const std::string& simulateChunk,
                        const std::string& heuristicChunk = std::string(),
                        const NamedArguments& args = {} )
    : ActionSimulator( args )
    , m_name( name )
    , m_evaluateChunk( evaluateChunk )
    , m_simulateChunk( simulateChunk )
    , m_heuristicChunk( heuristicChunk )
    , m_sandbox( std::make_shared< LuaSandbox >() )
    {
    }

    virtual ~LuaActionSimulator() = default;

    std::string_view name() const { return m_name; }
    StringHash hash() const override { return stringHasher( m_name ); }

    // @Return True if context meets prerequisites.
    bool evaluate( EvaluateSimulationParams params ) const override
    {
#if GIE_WITH_LUA
        return m_sandbox->executeEvaluate( m_evaluateChunk, params );
#else
        (void)params;
        return true;
#endif
    }

    // @Return True if simulation setup was successful.
    bool simulate( SimulateSimulationParams params ) const override
    {
#if GIE_WITH_LUA
        return m_sandbox->executeSimulate( m_simulateChunk, params );
#else
        (void)params;
        return true;
#endif
    }

    // Calculates heuristic value for simulation.
    void calculateHeuristic( CalculateHeuristicParams params ) const override
    {
#if GIE_WITH_LUA
        m_sandbox->executeHeuristic( m_heuristicChunk, params );
#else
        (void)params;
#endif
    }

private:
    std::string m_name;
    std::string m_evaluateChunk;
    std::string m_simulateChunk;
    std::string m_heuristicChunk;
    std::shared_ptr< LuaSandbox > m_sandbox;
};

// ActionSetEntry producing LuaActionSimulator instances.
class LuaActionSetEntry : public ActionSetEntry
{
public:
    LuaActionSetEntry( const std::string& name,
                       const std::string& evaluateChunk,
                       const std::string& simulateChunk,
                       const std::string& heuristicChunk = std::string(),
                       const NamedArguments& arguments = {} )
    : m_name( name )
    , m_evaluateChunk( evaluateChunk )
    , m_simulateChunk( simulateChunk )
    , m_heuristicChunk( heuristicChunk )
    , m_arguments( arguments )
    {
    }

    virtual ~LuaActionSetEntry() = default;

    std::string_view name() const override { return m_name; }
    StringHash hash() const override { return stringHasher( m_name ); }

    std::shared_ptr< ActionSimulator > simulator( const NamedArguments& /*arguments*/ ) const override
    {
        // Merge stored arguments with provided ones if needed (simple replace semantics).
        return std::make_shared< LuaActionSimulator >( m_name, m_evaluateChunk, m_simulateChunk, m_heuristicChunk, m_arguments );
    }

    std::shared_ptr< Action > action( const NamedArguments& /*arguments*/ ) const override
    {
        // No-op runtime Action - planner produces Actions via ActionSetEntry when backtracking.
        return nullptr;
    }

    const NamedArguments& entryArguments() const { return m_arguments; }

private:
    std::string m_name;
    std::string m_evaluateChunk;
    std::string m_simulateChunk;
    std::string m_heuristicChunk;
    NamedArguments m_arguments;
};

// Helper: append Lua action entries to a planner's actionSet.
// This keeps the existing C++ entries and adds Lua-defined ones.
inline void ApplyLuaActionEntriesToPlanner( Planner& planner, const std::vector< std::shared_ptr< ActionSetEntry > >& entries )
{
    auto& actionSet = planner.actionSet();
    actionSet.reserve( actionSet.size() + entries.size() );
    for( const auto& e : entries )
    {
        if( e ) actionSet.emplace_back( e );
    }
}

} // namespace gie
