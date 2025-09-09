-- EnterThrough.lua
function evaluate(params)
    debug("EnterThrough.evaluate called")
    return true
end

function simulate(params)
    debug("EnterThrough.simulate: move agent inside through entry point")
    local entry = entity_by_name("EntryPoint")
    if not entry then return false end
    -- move_agent_to_entity returns distance if success
    local d = move_agent_to_entity(entry)
    return d ~= nil
end

function heuristic(params)
    return 0
end
