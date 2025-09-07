#include <iostream>
#include <cassert>

#include <goapie_main.h>
#include "example.h"

#include <goapie_lua.h>
#include "world.h"
#include "agent.h"
#include "simulation.h"
#include "goal.h"
#include "common.h"

using namespace gie;

namespace
{
struct LuaIntegrationTestRunner
{
    LuaIntegrationTestRunner()
    {
        std::cout << "[LuaIntegrationTest] starting\n";

        // simple Lua chunk with evaluate/simulate/heuristic functions
        const std::string chunkName = "testAction";
        const std::string src =
            "function evaluate(params)\n"
            "  return true\n"
            "end\n"
            "\n"
            "function simulate(params)\n"
            "  return true\n"
            "end\n"
            "\n"
            "function heuristic(params)\n"
            "  return 42\n"
            "end\n";

        LuaSandbox sandbox;
        bool ok = sandbox.loadChunk( chunkName, src );
        if( !ok )
        {
            std::cout << "[LuaIntegrationTest] loadChunk failed\n";
            return;
        }

        // prepare minimal world/agent/simulation/goal objects
        World world{};
        Agent* a = world.createAgent( "lua_test_agent" );
        if( !a )
        {
            std::cout << "[LuaIntegrationTest] createAgent failed\n";
            return;
        }

        SimAgent simAgent( a );
        Simulation simulation( randGuid(), &world, &world.context(), simAgent );
        Goal goal( world );

        // Evaluate
        EvaluateSimulationParams evalParams( simulation, simAgent, goal );
        bool evalRes = sandbox.executeEvaluate( chunkName, evalParams );
        std::cout << "[LuaIntegrationTest] evaluate -> " << (evalRes ? "true" : "false") << "\n";
        assert( evalRes == true );

        // Simulate
        SimulateSimulationParams simParams( simulation, simAgent, goal );
        bool simRes = sandbox.executeSimulate( chunkName, simParams );
        std::cout << "[LuaIntegrationTest] simulate -> " << (simRes ? "true" : "false") << "\n";
        assert( simRes == true );

        // Heuristic
        CalculateHeuristicParams heurParams( simulation, simAgent, goal );
        // ensure heuristic field has a known starting value
        simulation.heuristic.value = -12345.f;
        sandbox.executeHeuristic( chunkName, heurParams );
        std::cout << "[LuaIntegrationTest] heuristic -> " << simulation.heuristic.value << "\n";
        assert( simulation.heuristic.value == 42.f );

        std::cout << "[LuaIntegrationTest] all checks passed\n";
    }
};

// instantiate to run before main
static LuaIntegrationTestRunner s_lua_integration_test_runner;
} // namespace
