-- UseKey.lua
function evaluate(params)
    debug("UseKey.evaluate called")
    return true
end

function simulate(params)
    debug("UseKey.simulate: attempt to consume Key item or set Safe unlocked")
    local key = entity_by_name("Key")
    if not key then return false end
    -- Example: set a property indicating key used
    return set_property(key, "Used", true)
end

function heuristic(params)
    return 0
end
