-- CrackSafe.lua
-- Crack a heavy-locked safe using a stethoscope.
-- Precondition: in safe room, LockMode=2 (heavy-lock), agent has Stethoscope.

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
        debug("CrackSafe.evaluate: not in safe room -> FALSE")
        return false
    end

    if get_property(safeGuid, "LockMode") ~= 2.0 then
        debug("CrackSafe.evaluate: not heavy-lock -> FALSE")
        return false
    end

    local inv = get_property(agentGuid, "Inventory") or {}
    local stethoInfo = entity_by_name("StethoscopeInfo")
    if not stethoInfo then return false end
    for _, itemGuid in ipairs(inv) do
        local info = get_property(itemGuid, "Info")
        if info and info == stethoInfo then
            debug("CrackSafe.evaluate: have stethoscope -> TRUE")
            return true
        end
    end

    debug("CrackSafe.evaluate: missing stethoscope -> FALSE")
    return false
end

function simulate(params)
    local safeGuid = entity_by_name("Safe")
    if not safeGuid then return false end
    set_property(safeGuid, "Open", true)
    set_cost(8.0)
    debug("CrackSafe.simulate: safe cracked")
    return true
end

function heuristic(params)
    local safeGuid = entity_by_name("Safe")
    if safeGuid and get_property(safeGuid, "Open") == true then return 0 end
    return 0
end
