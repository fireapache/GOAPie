-- OpenSafeWithCode.lua
-- Open the safe by entering collected code pieces.
-- Precondition: in safe room, LockMode=1 (code-lock), alarm disabled,
--               agent has collected enough code pieces (RequiredCodePieces).

local NullGuid = 0

function evaluate(params)
    local agentGuid = params.agent.guid

    -- Must be in the safe room
    local currentRoom = get_property(agentGuid, "CurrentRoom")
    if not currentRoom or currentRoom == NullGuid then
        debug("OpenSafeWithCode.evaluate: not inside -> FALSE")
        return false
    end

    local safeGuid = entity_by_name("Safe")
    if not safeGuid then return false end
    if get_property(safeGuid, "Open") == true then return false end

    local safeRoom = get_property(safeGuid, "InRoom")
    if currentRoom ~= safeRoom then
        debug("OpenSafeWithCode.evaluate: not in safe room -> FALSE")
        return false
    end

    if get_property(safeGuid, "LockMode") ~= 1.0 then
        debug("OpenSafeWithCode.evaluate: not code-lock -> FALSE")
        return false
    end

    -- Alarm must be disabled (code lock is wired to security system)
    local alarmGuid = entity_by_name("AlarmSystem")
    if alarmGuid and get_property(alarmGuid, "Armed") == true then
        debug("OpenSafeWithCode.evaluate: alarm still armed -> FALSE")
        return false
    end

    -- Safe must be inspected (agent discovered what's needed)
    if get_property(safeGuid, "Inspected") ~= true then
        debug("OpenSafeWithCode.evaluate: safe not inspected -> FALSE")
        return false
    end

    -- Check all RequiredItems satisfied by inventory (match by Info guid)
    local reqItems = get_property(safeGuid, "RequiredItems") or {}
    local inv = get_property(agentGuid, "Inventory") or {}
    for _, reqInfo in ipairs(reqItems) do
        local found = false
        for _, itemGuid in ipairs(inv) do
            local info = get_property(itemGuid, "Info")
            if info and info == reqInfo then found = true; break end
        end
        if not found then
            debug("OpenSafeWithCode.evaluate: missing required item -> FALSE")
            return false
        end
    end

    return true
end

function simulate(params)
    local safeGuid = entity_by_name("Safe")
    if not safeGuid then return false end
    set_property(safeGuid, "Open", true)
    set_cost(3.0)
    debug("OpenSafeWithCode.simulate: safe opened with code")
    return true
end

function heuristic(params)
    local NullGuid = 0
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
