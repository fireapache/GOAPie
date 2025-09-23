-- CrackSafe.lua
function evaluate(params)
    debug("CrackSafe.evaluate called")
    return true
end

function simulate(params)
    debug("CrackSafe.simulate: attempt to crack safe (placeholder)")
    local safe = entity_by_name("Safe")
    if not safe then return false end
    return set_property(safe, "Opened", true)
end

function heuristic(params)
    return 5
end
