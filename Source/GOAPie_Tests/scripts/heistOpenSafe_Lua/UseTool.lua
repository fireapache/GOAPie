-- UseTool.lua
-- Open the EnergyPanel cover (precondition for Interact).
-- Requires: EnergyPanel must be Known and its Open property must be false.

local NullGuid = 0

function evaluate(params)
    local panelGuid = entity_by_name("EnergyPanel")
    if not panelGuid then return false end

    -- Panel must be Known
    local knownSet = tag_set("Known") or {}
    local panelKnown = false
    for _, g in ipairs(knownSet) do if g == panelGuid then panelKnown = true; break end end
    if not panelKnown then return false end

    local panelOpen = get_property(panelGuid, "Open")
    if panelOpen == true then return false end  -- Already open

    debug("UseTool.evaluate: EnergyPanel known and closed -> TRUE")
    return true
end

function simulate(params)
    local agentGuid = params.agent.guid
    local panelGuid = entity_by_name("EnergyPanel")
    if not panelGuid then return false end

    local dist = move_agent_to_entity(panelGuid)
    set_property(panelGuid, "Open", true)
    set_cost((dist or 0) + 2.0)
    debug("UseTool.simulate: opened EnergyPanel cover, dist=" .. tostring(dist))
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
    local h = math.sqrt(dx*dx + dy*dy) * 0.1

    local alarmGuid = entity_by_name("AlarmSystem")
    if alarmGuid and get_property(alarmGuid, "Armed") == true then h = h + 20.0 end

    local panelGuid = entity_by_name("EnergyPanel")
    if panelGuid then
        local ks = tag_set("Known") or {}
        local pk = false
        for _, g in ipairs(ks) do if g == panelGuid then pk = true; break end end
        if not pk then h = h + 15.0 end
    end

    local currentRoom = get_property(agentGuid, "CurrentRoom")
    if not currentRoom or currentRoom == NullGuid then h = h + 10.0 end

    local required = get_property(safeGuid, "RequiredCodePieces") or 2
    local inv = get_property(agentGuid, "Inventory") or {}
    local have = 0
    local codeInfos = { entity_by_name("CodePieceAInfo"), entity_by_name("CodePieceBInfo"), entity_by_name("CodePieceCInfo") }
    for _, itemGuid in ipairs(inv) do
        local info = get_property(itemGuid, "Info")
        if info then for _, ci in ipairs(codeInfos) do if info == ci then have = have + 1; break end end end
    end
    h = h + math.max(0, required - have) * 15.0

    local studyDoorGuid = entity_by_name("StudyDoor")
    if studyDoorGuid and get_property(studyDoorGuid, "Locked") == true then h = h + 10.0 end

    local safeInspected = get_property(safeGuid, "Inspected")
    if safeInspected ~= nil and safeInspected ~= true then h = h + 5.0 end

    return h
end
