-- OpenSafeWithCode.lua
function evaluate(params)
    debug("OpenSafeWithCode.evaluate called")
    return true
end

function simulate(params)
    debug("OpenSafeWithCode.simulate: enter code to open safe (placeholder always succeeds)")
    local safe = entity_by_name("Safe")
    if not safe then return false end
    return set_property(safe, "Opened", true)
end

function heuristic(params)
    return 2
end
