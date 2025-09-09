#include <iostream>
#include <cassert>

#include "worldsetup_persistency.h"
#include <goapie_lua.h>

using namespace gie;

namespace
{
struct LuaEditorIntegrationTestRunner
{
    LuaEditorIntegrationTestRunner()
    {
        std::cout << "[LuaEditorIntegrationTest] starting\n";

        // Prepare a simple WorldSetupAction with evaluate/simulate sources.
        WorldSetupData data;
        WorldSetupAction a;
        a.name = "EditorAction";
        a.active = true;
        a.evaluateSource =
            "function evaluate(params)\n"
            "  return true\n"
            "end\n";
        a.simulateSource =
            "function simulate(params)\n"
            "  return true\n"
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
        assert( la.evaluateSource.find("function evaluate") != std::string::npos );
        assert( la.simulateSource.find("function simulate") != std::string::npos );

        std::cout << "[LuaEditorIntegrationTest] persistence verified\n";

        // Create a LuaActionSetEntry and apply the loaded sources, then compile/load.
        auto sandbox = std::make_shared< LuaSandbox >();

        // Use unified chunk name for this test
        const std::string chunkName = la.name + "_chunk";
        auto entry = std::make_shared< LuaActionSetEntry >( sandbox, la.name, chunkName, NamedArguments{} );

        // Attach the unified source (prefer evaluate source)
        entry->setSource( la.evaluateSource );

        bool compiled = entry->compileAndLoad();
        std::cout << "[LuaEditorIntegrationTest] compileAndLoad('" << la.name << "') -> " << (compiled ? "OK" : "FAIL") << "\n";
        assert( compiled );

        std::cout << "[LuaEditorIntegrationTest] all checks passed\n";
    }
};

// instantiate to run before main
static LuaEditorIntegrationTestRunner s_lua_editor_integration_test_runner;
} // namespace
