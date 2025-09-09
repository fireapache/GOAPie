-- Lockpick.lua
function evaluate(params)
    debug("Lockpick.evaluate called")
    return true
end

function simulate(params)
    debug("Lockpick.simulate: attempt lockpicking; success probabilistic placeholder")
    -- Placeholder: always succeed for now
    return true
end

function heuristic(params)
    return 1
end
