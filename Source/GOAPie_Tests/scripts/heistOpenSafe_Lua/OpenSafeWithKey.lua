-- OpenSafeWithKey.lua
function evaluate(params)
    debug("OpenSafeWithKey.evaluate called")
    local agentHasKey = get_property(entity_by_name("Agent"), "HasKey")
    if agentHasKey == nil then return true end
    return agentHasKey == true
end

function simulate(params)
    debug("OpenSafeWithKey.simulate: open safe using key if present")
    local safe = entity_by_name("Safe")
    if not safe then return false end
    return set_property(safe, "Opened", true)
end

function heuristic(params)
    return 0
end
