-- DisablePower.lua
-- Cut power at the fuse box to disable the alarm system.
-- Precondition: agent inside, alarm armed.

local NullGuid = 0

function evaluate(params)
    local agentGuid = params.agent.guid
    local currentRoom = get_property(agentGuid, "CurrentRoom")
    if not currentRoom or currentRoom == NullGuid then
        debug("DisablePower.evaluate: agent not inside -> FALSE")
        return false
    end

    local alarmGuid = entity_by_name("AlarmSystem")
    if not alarmGuid then return false end
    if get_property(alarmGuid, "Armed") ~= true then
        debug("DisablePower.evaluate: alarm not armed -> FALSE")
        return false
    end

    debug("DisablePower.evaluate -> TRUE")
    return true
end

function simulate(params)
    local agentGuid = params.agent.guid
    local fuseGuid = entity_by_name("FuseBoxEntity")
    local alarmGuid = entity_by_name("AlarmSystem")
    if not fuseGuid or not alarmGuid then return false end

    local dist = move_agent_to_entity(fuseGuid)
    set_property(alarmGuid, "Armed", false)
    set_cost((dist or 0) + 3.0)
    debug("DisablePower.simulate: power cut, dist=" .. tostring(dist))
    return true
end

function heuristic(params)
    local agentGuid = params.agent.guid
    local safeGuid = entity_by_name("Safe")
    if not safeGuid then return 0 end
    if get_property(safeGuid, "Open") == true then return 0 end

    local agentLoc = get_property(agentGuid, "Location")
    local safeRoom = get_property(safeGuid, "InRoom")
    if not agentLoc or not safeRoom or safeRoom == NullGuid then return 0 end
    local roomLoc = get_property(safeRoom, "Location")
    if not roomLoc then return 0 end

    local dx = agentLoc[1] - roomLoc[1]
    local dy = agentLoc[2] - roomLoc[2]
    local h = math.sqrt(dx*dx + dy*dy)

    local lockMode = get_property(safeGuid, "LockMode")
    if lockMode == 1.0 then
        local required = get_property(safeGuid, "RequiredCodePieces") or 2
        local inv = get_property(agentGuid, "Inventory") or {}
        local have = 0
        local codeInfos = { entity_by_name("CodePieceAInfo"), entity_by_name("CodePieceBInfo"), entity_by_name("CodePieceCInfo") }
        for _, itemGuid in ipairs(inv) do
            local info = get_property(itemGuid, "Info")
            if info then for _, ci in ipairs(codeInfos) do if info == ci then have = have + 1; break end end end
        end
        h = h + math.max(0, required - have) * 15.0
        local alarmGuid = entity_by_name("AlarmSystem")
        if alarmGuid and get_property(alarmGuid, "Armed") == true then h = h + 10.0 end
    end

    return h
end
