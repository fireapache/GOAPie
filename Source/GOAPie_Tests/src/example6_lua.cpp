// File: Source/GOAPie_Tests/src/example6_lua.cpp
// Loads Lua action chunks and registers Lua-backed action entries.
// Updated for multi-plan discovery scenario (matching example6.cpp).

#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

#include <goapie.h>
#include "goapie_lua.h"

#include "example.h"

#ifdef _WIN32
#include <windows.h>
#endif

const char* heistOpenSafe_LuaDescription()
{
	return "Heist discovery example, but actions defined by external Lua scripts loaded from examples/<name>/scripts/";
}

// set in main.cpp
extern std::string g_exampleName;

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

    // Try new directory structure first, then fallback to legacy
    std::string scriptsBase;
	const std::string exampleName = g_exampleName;

    // New structure: <exeDir>/examples/example6/scripts/
    std::filesystem::path exeDir;
    if (!exePath.empty()) {
        exeDir = std::filesystem::path( exePath );
        // If exePath was a full path including the executable file, remove the filename
        if (std::filesystem::is_regular_file(exeDir)) exeDir = exeDir.parent_path();

        std::filesystem::path newPath = exeDir / "examples" / exampleName / "scripts";
        if (std::filesystem::exists(newPath)) {
            scriptsBase = newPath.string();
            // Ensure scriptsBase ends with the native directory separator so later concatenation works
            if (!scriptsBase.empty() && scriptsBase.back() != std::filesystem::path::preferred_separator) {
                scriptsBase.push_back(static_cast<char>(std::filesystem::path::preferred_separator));
            }
            std::cout << "[example6_lua] Using new directory structure: " << scriptsBase << std::endl;
        }
    }

    // Discover .lua files in scriptsBase and build actions from their filenames
	std::vector< std::filesystem::path > luaFiles;

    if( scriptsBase.empty() )
    {
		std::cout << "[example6_lua] New directory structure not found!\n";
	}
    else
    {
		try
		{
			for( const auto& entry : std::filesystem::directory_iterator( std::filesystem::path( scriptsBase ) ) )
			{
				try
				{
					if( !entry.is_regular_file() )
						continue;
					auto ext = entry.path().extension().string();
					if( ext == ".lua" )
						luaFiles.emplace_back( entry.path() );
				}
				catch( const std::exception& /*e*/ )
				{
					// skip problematic entries
				}
			}
		}
		catch( const std::exception& /*e*/ )
		{
			std::cout << "[example6_lua] failed to enumerate scripts in: " << scriptsBase << std::endl;
		}
    }

    if( luaFiles.empty() )
	{
		std::cout << "[example6_lua] No .lua files found!\n";
	}

    // Sort discovered files by filename to have deterministic order
    std::sort( luaFiles.begin(), luaFiles.end() );

    // Actions that must be marked as forceLeaf (opaque during planning)
    std::set<std::string> forceLeafActions = { "Observe", "Inspect" };

    std::vector< std::shared_ptr< ActionSetEntry > > luaEntries;
    for (const auto& p : luaFiles) {
        std::string name = p.stem().string();
        std::string file = p.string();
        std::string src = readFileContents( file );

        // Use unified chunk name per-action
        std::string luaChunk = g_exampleName + std::string{ "." } + name;

        // New single-chunk constructor
        auto entry = std::make_shared< LuaActionSetEntry >( sandbox, name, luaChunk, NamedArguments{} );

        // Mark Observe as forceLeaf so the planner treats it as opaque
        if( forceLeafActions.count( name ) )
            entry->setForceLeaf( true );

        if (src.empty()) {
            std::cout << "[example6_lua] script missing or empty: " << file << "\n";
            // leave entry source empty (permissive entry)
        }
        else {
            // Attach the source to the entry so the UI/editor can show and edit it.
            entry->setSource( src );

            // Compile/validate and load sources into the shared sandbox under the configured chunk name.
            bool ok = entry->compile();
            std::cout << "[example6_lua] compile('" << name << "') -> " << (ok ? "OK" : "FAIL") << "\n";
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
