// File: Source/GOAPie_Tests/src/example6_lua.cpp
// Starter implementation: loads Lua action chunks and registers Lua-backed action entries.
// Updated to use single-chunk LuaActionSetEntry API.

#include <fstream>
#include <sstream>
#include <iostream>

#include <goapie.h>
#include "goapie_lua.h"

#include "example.h"

#ifdef _WIN32
#include <windows.h>
#endif

 // forward-declare C++ example setup from example6.cpp
 gie::Agent* heistOpenSafe_world( ExampleParameters& params );

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
    gie::Agent* agent = heistOpenSafe_world( params );

    // Create a shared Lua sandbox and load scripts from scripts/example6_lua/
    auto sandbox = std::make_shared< LuaSandbox >();

    std::string exePath;
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
    {
        exePath.assign(buf, buf + len);
        // keep trailing slash after removing file name
        size_t pos = exePath.find_last_of("\\/");
        if (pos != std::string::npos) exePath.resize(pos + 1);
    }
    else
    {
        exePath.clear();
    }
#endif

    const std::string scriptsBase = exePath.empty()
        ? std::string("Source/GOAPie_Tests/scripts/example6_lua/")
        : (exePath + std::string("scripts/example6_lua/"));
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

        // Use unified chunk name per-action
        std::string luaChunk = std::string("example6.") + name;

        // New single-chunk constructor
        auto entry = std::make_shared< LuaActionSetEntry >( sandbox, name, luaChunk, NamedArguments{} );

        if( src.empty() )
        {
            std::cout << "[example6_lua] script missing or empty: " << file << "\n";
            // leave entry source empty (permissive entry)
        }
        else
        {
            // Attach the source to the entry so the UI/editor can show and edit it.
            entry->setSource( src );

            // Compile/validate and load sources into the shared sandbox under the configured chunk name.
            bool ok = entry->compileAndLoad();
            std::cout << "[example6_lua] compileAndLoad('" << name << "') -> " << (ok ? "OK" : "FAIL") << "\n";
        }

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
    // Lua actions participate in planning. Use the agent returned from world setup.
    if( agent )
    {
        params.planner.simulate( params.goal, *agent );
    }
    else
    {
        std::cout << "[example6_lua] no agent created by heistOpenSafe_world(), skipping simulate()\n";
    }

    return 0;
}
