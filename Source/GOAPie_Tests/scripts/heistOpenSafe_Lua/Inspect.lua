-- Inspect.lua
-- Examine a Known entity to discover its RequiredItems.
-- This is a forceLeaf action — opaque during planning.
-- Only sets ExploredNewArea=true during simulate; real effects happen at execution.

local NullGuid = 0

function evaluate(params)
    local agentGuid = params.agent.guid

    -- Agent must be inside
    local currentRoom = get_property(agentGuid, "CurrentRoom")
    if not currentRoom or currentRoom == NullGuid then
        debug("Inspect.evaluate: not inside -> FALSE")
        return false
    end

    -- Find a Known entity with RequiredItems and Inspected=false
    local knownSet = tag_set("Known") or {}
    for _, guid in ipairs(knownSet) do
        local reqItems = get_property(guid, "RequiredItems")
        if reqItems and #reqItems > 0 then
            local inspected = get_property(guid, "Inspected")
            if inspected == false then
                debug("Inspect.evaluate: found inspectable entity -> TRUE")
                return true
            end
        end
    end

    debug("Inspect.evaluate: no inspectable entities -> FALSE")
    return false
end

function simulate(params)
    local agentGuid = params.agent.guid
    local agentLoc = get_property(agentGuid, "Location")
    local knownSet = tag_set("Known") or {}

    -- Find nearest inspectable entity
    local bestGuid, bestDist = nil, math.huge
    for _, guid in ipairs(knownSet) do
        local reqItems = get_property(guid, "RequiredItems")
        if reqItems and #reqItems > 0 then
            local inspected = get_property(guid, "Inspected")
            if inspected == false then
                local loc = get_property(guid, "Location")
                if loc and agentLoc then
                    local dx = agentLoc[1] - loc[1]
                    local dy = agentLoc[2] - loc[2]
                    local d = math.sqrt(dx*dx + dy*dy)
                    if d < bestDist then bestDist = d; bestGuid = guid end
                end
            end
        end
    end

    if not bestGuid then return false end

    local dist = move_agent_to_entity(bestGuid)
    set_property(agentGuid, "ExploredNewArea", true)
    set_cost((dist or 0) + 2.0)
    debug("Inspect.simulate: inspected entity, dist=" .. tostring(dist))
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
