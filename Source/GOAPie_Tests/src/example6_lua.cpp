// File: Source/GOAPie_Tests/src/example6_lua.cpp
// Starter implementation: loads Lua action chunks and registers Lua-backed action entries.
// This is an incremental skeleton — world construction is reused from the C++ example
// by calling heistOpenSafe(...) so you can iterate on Lua bindings and scripts quickly.

#include <fstream>
#include <sstream>
#include <iostream>

#include <goapie.h>
#include "goapie_lua.h"

#include "example.h"

// forward-declare C++ example setup from example6.cpp
int heistOpenSafe( ExampleParameters& params );

using namespace gie;

static std::string readFileContents( const std::string& path )
{
    std::ifstream ifs( path, std::ios::in | std::ios::binary );
    if( !ifs ) return std::string();
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

int heistOpenSafe_Lua( ExampleParameters& params )
{
    // Reuse the existing C++ world setup to avoid duplicated world construction:
    // heistOpenSafe() registers C++ ActionSetEntries and runs an initial simulate.
    // This skeleton calls it to build the world, then injects Lua-backed entries
    // so you can iterate on Lua scripts and bindings. Later we will replace the
    // C++ registrations entirely and move the world setup into this file if needed.
    heistOpenSafe( params );

    // Create a shared Lua sandbox and load scripts from scripts/example6_lua/
    auto sandbox = std::make_shared< LuaSandbox >();

    const std::string scriptsBase = "Source/GOAPie_Tests/scripts/example6_lua/";
    const std::vector< std::string > actionNames =
    {
        "DisableAlarm",
        "DisablePower",
        "UseKey",
        "Lockpick",
        "BreakConnector",
        "CutBars",
        "EnterThrough",
        "SearchForItem",
        "MoveInside",
        "OpenSafeWithKey",
        "OpenSafeWithCode",
        "CrackSafe",
        "BruteForceSafe"
    };

    std::vector< std::shared_ptr< ActionSetEntry > > luaEntries;
    for( const auto& name : actionNames )
    {
        std::string file = scriptsBase + name + ".lua";
        std::string src = readFileContents( file );
        if( src.empty() )
        {
            std::cout << "[example6_lua] script missing or empty: " << file << "\n";
            // still create a placeholder entry (permissive behavior) so the planner can run
            // The LuaActionSetEntry will work even if chunk isn't loaded.
        }
        else
        {
            bool ok = sandbox->loadChunk( name, src );
            std::cout << "[example6_lua] loadChunk('" << name << "') -> " << (ok ? "OK" : "FAIL") << "\n";
        }

        // Create a LuaActionSetEntry that uses the shared sandbox
        auto entry = std::make_shared< LuaActionSetEntry >( sandbox, name, name, name, name, NamedArguments{} );
        luaEntries.emplace_back( entry );
    }

    // Replace planner's action set with Lua-defined entries only (Lua-only mode).
    {
        auto& actionSet = params.planner.actionSet();
        actionSet.clear();
        actionSet.reserve( luaEntries.size() );
        for( const auto& e : luaEntries )
        {
            if( e ) actionSet.emplace_back( e );
        }
    }

    // Re-run planning/simulation using the same goal & agent so the newly-registered
    // Lua actions participate in planning. Use the current planner.agent() if available.
    if( auto agent = params.planner.agent() )
    {
        params.planner.simulate( params.goal, *agent );
    }
    else
    {
        std::cout << "[example6_lua] no agent registered in planner, skipping simulate()\n";
    }

    return 0;
}
