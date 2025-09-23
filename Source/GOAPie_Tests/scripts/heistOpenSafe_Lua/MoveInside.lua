-- MoveInside.lua
function evaluate(params)
    debug("MoveInside.evaluate called")
    return true
end

function simulate(params)
    debug("MoveInside.simulate: move agent to Safe location")
    local safe = entity_by_name("Safe")
    if not safe then return false end
    local d = move_agent_to_entity(safe)
    return d ~= nil
end

function heuristic(params)
    -- small heuristic based on distance (real heuristic function exists too)
    return 0
end
