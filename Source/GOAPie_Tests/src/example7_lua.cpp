// File: Source/GOAPie_Tests/src/example7_lua.cpp
// Lua variant of Example 7 (Treasure Hunt): loads action definitions from external Lua scripts.

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

const char* treasureHunt_LuaDescription()
{
	return "Treasure hunt example, but actions defined by external Lua scripts loaded from examples/<name>/scripts/";
}

// set in main.cpp
extern std::string g_exampleName;

// forward-declare C++ example setup from example7.cpp
gie::Agent* treasureHunt_world( ExampleParameters& params );

using namespace gie;

// FindEntityByName helper (same logic as example7.cpp)
static gie::Entity* FindEntityByName( gie::Blackboard& ctx, const char* name )
{
	for( const auto& kv : ctx.entities() )
		if( gie::stringRegister().get( kv.second.nameHash() ) == name )
			return ctx.entity( kv.first );
	const gie::Blackboard* p = ctx.parent();
	while( p )
	{
		for( const auto& kv : p->entities() )
			if( gie::stringRegister().get( kv.second.nameHash() ) == name )
				return ctx.entity( kv.first );
		p = p->parent();
	}
	return nullptr;
}

static std::string readFileContents( const std::string& path )
{
	std::ifstream ifs( path, std::ios::in | std::ios::binary );
	if( !ifs ) return std::string();
	std::ostringstream ss;
	ss << ifs.rdbuf();
	return ss.str();
}

// Actions that should be marked as forceLeaf (opaque during planning)
static bool isForceLeafAction( const std::string& name )
{
	return name == "Observe" || name == "Inspect";
}

int treasureHunt_Lua( ExampleParameters& params )
{
	// Reuse the existing C++ world setup:
	gie::Agent* agent = treasureHunt_world( params );

	// Create a shared Lua sandbox
	auto sandbox = std::make_shared< LuaSandbox >();

	std::string exePath;
#ifdef _WIN32
	char buf[MAX_PATH];
	DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
	if (len > 0 && len < MAX_PATH)
	{
		exePath.assign(buf, buf + len);
		size_t pos = exePath.find_last_of("\\/");
		if (pos != std::string::npos) exePath.resize(pos + 1);
	}
	else
	{
		exePath.clear();
	}
#endif

	// Find scripts directory
	std::string scriptsBase;
	const std::string exampleName = g_exampleName;

	std::filesystem::path exeDir;
	if (!exePath.empty()) {
		exeDir = std::filesystem::path( exePath );
		if (std::filesystem::is_regular_file(exeDir)) exeDir = exeDir.parent_path();

		std::filesystem::path newPath = exeDir / "examples" / exampleName / "scripts";
		if (std::filesystem::exists(newPath)) {
			scriptsBase = newPath.string();
			if (!scriptsBase.empty() && scriptsBase.back() != std::filesystem::path::preferred_separator) {
				scriptsBase.push_back(static_cast<char>(std::filesystem::path::preferred_separator));
			}
			std::cout << "[example7_lua] Using directory: " << scriptsBase << std::endl;
		}
	}

	// Discover .lua files
	std::vector< std::filesystem::path > luaFiles;

	if( scriptsBase.empty() )
	{
		std::cout << "[example7_lua] Scripts directory not found!\n";
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
				}
			}
		}
		catch( const std::exception& /*e*/ )
		{
			std::cout << "[example7_lua] failed to enumerate scripts in: " << scriptsBase << std::endl;
		}
	}

	if( luaFiles.empty() )
	{
		std::cout << "[example7_lua] No .lua files found!\n";
	}

	// Sort for deterministic order
	std::sort( luaFiles.begin(), luaFiles.end() );

	std::vector< std::shared_ptr< ActionSetEntry > > luaEntries;
	for (const auto& p : luaFiles) {
		std::string name = p.stem().string();
		std::string file = p.string();
		std::string src = readFileContents( file );

		std::string luaChunk = g_exampleName + std::string{ "." } + name;

		auto entry = std::make_shared< LuaActionSetEntry >( sandbox, name, luaChunk, NamedArguments{} );

		// Set forceLeaf for opaque actions (Observe, Inspect)
		if( isForceLeafAction( name ) )
		{
			entry->setForceLeaf( true );
		}

		if (src.empty()) {
			std::cout << "[example7_lua] script missing or empty: " << file << "\n";
		}
		else {
			entry->setSource( src );

			bool ok = entry->compile();
			std::cout << "[example7_lua] compile('" << name << "') -> " << (ok ? "OK" : "FAIL") << "\n";
		}

		luaEntries.emplace_back( entry );
	}

	// Replace planner's action set with Lua-defined entries
	{
		auto& actionSet = params.planner.actionSet();
		actionSet.clear();
		actionSet.reserve( luaEntries.size() );
		for( const auto& e : luaEntries )
		{
			if( e ) actionSet.emplace_back( e );
		}
	}

	// Set up planner depth limit
	params.planner.depthLimitMutator() = 20;

	// Set primary goal: open the chest
	auto chest = FindEntityByName( params.world.context(), "LockedChest" );
	if( chest )
		params.goal.targets.emplace_back( chest->property( "Open" )->guid(), true );

	// Run simulation with Lua actions
	if( agent )
	{
		params.planner.simulate( params.goal, *agent );
	}
	else
	{
		std::cout << "[example7_lua] no agent created by treasureHunt_world(), skipping simulate()\n";
	}

	return 0;
}

int treasureHunt_LuaValidateResult( std::string& failMsg )
{
	// Basic validation: set up world, load Lua actions, run planner
	gie::World world{};
	gie::Planner planner{};
	gie::Goal goal{ world };
	ExampleParameters params{ world, planner, goal };
	params.visualize = false;

	treasureHunt_Lua( params );

	// The planner should have generated simulations
	if( planner.simulations().empty() )
	{
		failMsg = "No simulations generated by Lua variant";
		return 1;
	}

	return 0;
}
