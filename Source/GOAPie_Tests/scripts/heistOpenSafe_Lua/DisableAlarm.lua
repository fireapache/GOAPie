-- DisableAlarm.lua
-- Disable the alarm by reaching the alarm panel (in Corridor).
-- Precondition: agent must be inside the house and alarm must be armed.

local NullGuid = 0

function evaluate(params)
    local agentGuid = params.agent.guid
    local currentRoom = get_property(agentGuid, "CurrentRoom")
    if not currentRoom or currentRoom == NullGuid then
        debug("DisableAlarm.evaluate: agent not inside -> FALSE")
        return false
    end

    local alarmGuid = entity_by_name("AlarmSystem")
    if not alarmGuid then
        debug("DisableAlarm.evaluate: AlarmSystem not found -> FALSE")
        return false
    end

    local armed = get_property(alarmGuid, "Armed")
    if armed ~= true then
        debug("DisableAlarm.evaluate: alarm not armed -> FALSE")
        return false
    end

    debug("DisableAlarm.evaluate -> TRUE")
    return true
end

function simulate(params)
    local agentGuid = params.agent.guid
    local panelGuid = entity_by_name("AlarmPanel")
    local alarmGuid = entity_by_name("AlarmSystem")
    if not panelGuid or not alarmGuid then
        debug("DisableAlarm.simulate: missing panel or system -> FALSE")
        return false
    end

    local dist = move_agent_to_entity(panelGuid)
    debug("DisableAlarm.simulate: moved to panel, dist=" .. tostring(dist))
    set_property(alarmGuid, "Armed", false)
    set_cost((dist or 0) + 2.0)
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

    -- Penalty for missing code pieces
    local lockMode = get_property(safeGuid, "LockMode")
    if lockMode == 1.0 then
        local required = get_property(safeGuid, "RequiredCodePieces") or 2
        local inv = get_property(agentGuid, "Inventory") or {}
        local have = 0
        local codeInfos = {
            entity_by_name("CodePieceAInfo"),
            entity_by_name("CodePieceBInfo"),
            entity_by_name("CodePieceCInfo")
        }
        for _, itemGuid in ipairs(inv) do
            local info = get_property(itemGuid, "Info")
            if info then
                for _, ci in ipairs(codeInfos) do
                    if info == ci then have = have + 1; break end
                end
            end
        end
        local missing = math.max(0, required - have)
        h = h + missing * 15.0

        local alarmGuid = entity_by_name("AlarmSystem")
        if alarmGuid and get_property(alarmGuid, "Armed") == true then
            h = h + 10.0
        end
    end

    return h
end
