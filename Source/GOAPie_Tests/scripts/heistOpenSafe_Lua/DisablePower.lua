-- DisablePower.lua
function evaluate(params)
    debug("DisablePower.evaluate called")
    return true
end

function simulate(params)
    debug("DisablePower.simulate: setting Power.Enabled = false if present")
    local p = entity_by_name("Power")
    if not p then return false end
    return set_property(p, "Enabled", false)
end

function heuristic(params)
    return 0
end
