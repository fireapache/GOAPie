-- BruteForceSafe.lua
function evaluate(params)
    debug("BruteForceSafe.evaluate called")
    return true
end

function simulate(params)
    debug("BruteForceSafe.simulate: brute-force opening the safe (placeholder)")
    local safe = entity_by_name("Safe")
    if not safe then return false end
    return set_property(safe, "Opened", true)
end

function heuristic(params)
    return 10
end
