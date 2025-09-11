-- SearchForItem.lua
function evaluate(params)
    debug("SearchForItem.evaluate called")
    return true
end

function simulate(params)
    debug("SearchForItem.simulate: set property FoundItem=true on Agent if item exists")
    local item = entity_by_name("Key") -- example target
    if not item then return false end
    local agentGuid = entity_by_name("Agent")
    if not agentGuid then return false end
    return set_property(agentGuid, "HasKey", true)
end

function heuristic(params)
    return 1
end
