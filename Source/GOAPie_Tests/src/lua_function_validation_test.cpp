// Test file to verify Lua function call validation
#include "goapie_lua.h"
#include <iostream>
#include <cassert>

using namespace gie;

void testValidFunctionCalls()
{
    std::cout << "Testing valid function calls...\n";

    auto sandbox = std::make_shared<LuaSandbox>();
    auto entry = std::make_shared<LuaActionSetEntry>(sandbox, "test_action", "test_chunk");

    // Test valid goapie_lua functions
    std::string validScript = R"(
function evaluate(params)
    local agent_pos = get_property(params.agent.guid, "Location")
    local target = entity_by_name("safe")
    local agents = tag_set("agent")
    debug("Planning to move to safe")
    return true
end

function simulate(params)
    local success = set_property(params.agent.guid, "Location", {1, 2, 3})
    move_agent_to_entity(target)
    return success
end

function heuristic(params)
    return 0
end
)";

    entry->setSource(validScript);
    bool result = entry->compile();

    std::cout << "Valid script compilation result: " << (result ? "SUCCESS" : "FAILED") << "\n";
    if (!result) {
        std::cout << "Error: " << entry->lastCompileError() << "\n";
    }
    assert(result && "Valid script should compile successfully");
}

void testInvalidFunctionCalls()
{
    std::cout << "Testing invalid function calls...\n";

    auto sandbox = std::make_shared<LuaSandbox>();
    auto entry = std::make_shared<LuaActionSetEntry>(sandbox, "test_action", "test_chunk");

    // Test invalid function call
    std::string invalidScript = R"(
function evaluate(params)
    local result = dangerous_function()  -- This should be rejected
    return result
end
)";

    entry->setSource(invalidScript);
    bool result = entry->compile();

    std::cout << "Invalid script compilation result: " << (result ? "SUCCESS" : "FAILED") << "\n";
    if (!result) {
        std::cout << "Error: " << entry->lastCompileError() << "\n";
    }
    assert(!result && "Invalid script should fail compilation");
}

void testLocalFunctionDefinitions()
{
    std::cout << "Testing local function definitions...\n";

    auto sandbox = std::make_shared<LuaSandbox>();
    auto entry = std::make_shared<LuaActionSetEntry>(sandbox, "test_action", "test_chunk");

    // Test local function definitions (should be allowed)
    std::string localFunctionScript = R"(
local function helper_function()
    return true
end

function evaluate(params)
    local result = helper_function()  -- This should be allowed
    return result
end
)";

    entry->setSource(localFunctionScript);
    bool result = entry->compile();

    std::cout << "Local function script compilation result: " << (result ? "SUCCESS" : "FAILED") << "\n";
    if (!result) {
        std::cout << "Error: " << entry->lastCompileError() << "\n";
    }
    assert(result && "Local function definitions should be allowed");
}

int main()
{
    std::cout << "Running Lua function validation tests...\n\n";

    try {
        testValidFunctionCalls();
        std::cout << "✓ Valid function calls test passed\n\n";

        testInvalidFunctionCalls();
        std::cout << "✓ Invalid function calls test passed\n\n";

        testLocalFunctionDefinitions();
        std::cout << "✓ Local function definitions test passed\n\n";

        std::cout << "All tests passed! ✓\n";
    }
    catch (const std::exception& e) {
        std::cout << "Test failed with exception: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
