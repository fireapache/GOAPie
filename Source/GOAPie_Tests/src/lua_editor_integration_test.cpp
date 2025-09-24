#include <iostream>
#include <cassert>

#include "worldsetup_persistency.h"
#include <goapie_lua.h>

using namespace gie;

void RunLuaEditorIntegrationTest()
{
    std::cout << "[LuaEditorIntegrationTest] starting\n";

    // Prepare a simple WorldSetupAction with unified lua source.
    WorldSetupData data;
    WorldSetupAction a;
    a.name = "EditorAction";
    a.active = true;
    a.luaSource =
        "function evaluate(params)\n"
        "  return true\n"
        "end\n"
        "\n"
        "function simulate(params)\n"
        "  return true\n"
        "end\n"
        "function heuristic(params)\n"
        "  return 1\n"
        "end\n";

    data.actions.emplace_back( std::move(a) );

    const std::string fileName = "lua_editor_test_worldsetup.json";

    // Save to disk
    bool saveOk = SaveWorldSetupToJson( data, fileName );
    if( !saveOk )
    {
        std::cout << "[LuaEditorIntegrationTest] SaveWorldSetupToJson failed\n";
        assert(false);
        return;
    }

    // Load back
    WorldSetupData loaded;
    bool loadOk = LoadWorldSetupFromJson( loaded, fileName );
    if( !loadOk )
    {
        std::cout << "[LuaEditorIntegrationTest] LoadWorldSetupFromJson failed\n";
        assert(false);
        return;
    }

    // Validate persisted content
    assert( loaded.actions.size() == 1 );
    const auto& la = loaded.actions[0];
    assert( la.name == "EditorAction" );
    assert( la.luaSource.find("function evaluate") != std::string::npos );
    assert( la.luaSource.find("function simulate") != std::string::npos );

    std::cout << "[LuaEditorIntegrationTest] persistence verified\n";

    // Create a LuaActionSetEntry and apply the loaded sources, then compile/load.
    auto sandbox = std::make_shared< LuaSandbox >();

    // Use unified chunk name for this test
    const std::string chunkName = la.name + "_chunk";
    auto entry = std::make_shared< LuaActionSetEntry >( sandbox, la.name, chunkName, NamedArguments{} );

    // Attach the unified source
    entry->setSource( la.luaSource );

    bool compiled = entry->compile();
    std::cout << "[LuaEditorIntegrationTest] compile('" << la.name << "') -> " << (compiled ? "OK" : "FAIL") << "\n";
    if( !compiled )
    {
		std::cout << entry->lastCompileError() << "\n";
    }
    assert( compiled );

    std::cout << "[LuaEditorIntegrationTest] all checks passed\n";
}
