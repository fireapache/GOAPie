-- CutBars.lua
function evaluate(params)
    debug("CutBars.evaluate called")
    return true
end

function simulate(params)
    debug("CutBars.simulate: cut bars to allow entry")
    local bars = entity_by_name("Bars")
    if not bars then return false end
    return set_property(bars, "Cut", true)
end

function heuristic(params)
    return 1
end
