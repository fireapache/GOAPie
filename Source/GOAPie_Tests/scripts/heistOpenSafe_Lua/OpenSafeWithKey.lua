-- OpenSafeWithKey.lua
-- Open the safe using a physical key.
-- Precondition: in safe room, LockMode=0 (key-lock), agent has SafeKey.

local NullGuid = 0

function evaluate(params)
    local agentGuid = params.agent.guid

    -- Must be in the safe room
    local currentRoom = get_property(agentGuid, "CurrentRoom")
    if not currentRoom or currentRoom == NullGuid then
        debug("OpenSafeWithKey.evaluate: not inside -> FALSE")
        return false
    end

    local safeGuid = entity_by_name("Safe")
    if not safeGuid then return false end
    if get_property(safeGuid, "Open") == true then return false end

    local safeRoom = get_property(safeGuid, "InRoom")
    if currentRoom ~= safeRoom then
        debug("OpenSafeWithKey.evaluate: not in safe room -> FALSE")
        return false
    end

    if get_property(safeGuid, "LockMode") ~= 0.0 then
        debug("OpenSafeWithKey.evaluate: not key-lock -> FALSE")
        return false
    end

    -- Check for SafeKey in inventory
    local inv = get_property(agentGuid, "Inventory") or {}
    local safeKeyInfo = entity_by_name("SafeKeyInfo")
    if not safeKeyInfo then return false end
    for _, itemGuid in ipairs(inv) do
        local info = get_property(itemGuid, "Info")
        if info and info == safeKeyInfo then
            debug("OpenSafeWithKey.evaluate: have SafeKey -> TRUE")
            return true
        end
    end

    debug("OpenSafeWithKey.evaluate: missing SafeKey -> FALSE")
    return false
end

function simulate(params)
    local safeGuid = entity_by_name("Safe")
    if not safeGuid then return false end
    set_property(safeGuid, "Open", true)
    set_cost(2.0)
    debug("OpenSafeWithKey.simulate: safe opened with key")
    return true
end

function heuristic(params)
    local safeGuid = entity_by_name("Safe")
    if safeGuid and get_property(safeGuid, "Open") == true then return 0 end
    return 0
end
