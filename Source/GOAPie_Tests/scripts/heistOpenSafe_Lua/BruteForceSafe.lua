-- BruteForceSafe.lua
-- Brute-force the safe open using a crowbar. Very high cost — last resort.
-- Precondition: in safe room, agent has Crowbar.

local NullGuid = 0

function evaluate(params)
    local agentGuid = params.agent.guid

    local currentRoom = get_property(agentGuid, "CurrentRoom")
    if not currentRoom or currentRoom == NullGuid then return false end

    local safeGuid = entity_by_name("Safe")
    if not safeGuid then return false end
    if get_property(safeGuid, "Open") == true then return false end

    local safeRoom = get_property(safeGuid, "InRoom")
    if currentRoom ~= safeRoom then
        debug("BruteForceSafe.evaluate: not in safe room -> FALSE")
        return false
    end

    -- Requires crowbar
    local inv = get_property(agentGuid, "Inventory") or {}
    local crowbarInfo = entity_by_name("CrowbarInfo")
    if not crowbarInfo then return false end
    for _, itemGuid in ipairs(inv) do
        local info = get_property(itemGuid, "Info")
        if info and info == crowbarInfo then
            debug("BruteForceSafe.evaluate: have crowbar -> TRUE")
            return true
        end
    end

    debug("BruteForceSafe.evaluate: missing crowbar -> FALSE")
    return false
end

function simulate(params)
    local safeGuid = entity_by_name("Safe")
    if not safeGuid then return false end
    set_property(safeGuid, "Open", true)
    set_cost(200.0)
    debug("BruteForceSafe.simulate: safe forced open")
    return true
end

function heuristic(params)
    local safeGuid = entity_by_name("Safe")
    if safeGuid and get_property(safeGuid, "Open") == true then return 0 end
    return 0
end
