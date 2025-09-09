-- BreakConnector.lua
function evaluate(params)
    debug("BreakConnector.evaluate called")
    return true
end

function simulate(params)
    debug("BreakConnector.simulate: break connector -> set Broken=true")
    local conn = entity_by_name("Connector")
    if not conn then return false end
    return set_property(conn, "Broken", true)
end

function heuristic(params)
    return 1
end
