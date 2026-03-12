-- Observe.lua
-- Stand at a Known waypoint and reveal what it shows.
-- This action is forceLeaf (set in C++ loader) — opaque during planning.
-- Only sets ExploredNewArea=true during simulate; real reveals happen at execution.

local NullGuid = 0

function evaluate(params)
    local agentGuid = params.agent.guid
    local agentLoc = get_property(agentGuid, "Location")
    if not agentLoc then return false end

    local waypoints = tag_set("Waypoint") or {}
    local knownSet = tag_set("Known") or {}

    -- Build Known lookup
    local isKnown = {}
    for _, g in ipairs(knownSet) do isKnown[g] = true end

    for _, wpGuid in ipairs(waypoints) do
        if isKnown[wpGuid] then
            local wpLoc = get_property(wpGuid, "Location")
            if wpLoc then
                local dx = agentLoc[1] - wpLoc[1]
                local dy = agentLoc[2] - wpLoc[2]
                if math.sqrt(dx*dx + dy*dy) < 1.0 then
                    local reveals = get_property(wpGuid, "Reveals") or {}
                    for _, rg in ipairs(reveals) do
                        if not isKnown[rg] then
                            return true  -- Found unrevealed entity
                        end
                    end
                end
            end
        end
    end
    return false
end

function simulate(params)
    local agentGuid = params.agent.guid
    local explored = get_property(agentGuid, "ExploredNewArea")
    set_property(agentGuid, "ExploredNewArea", true)
    set_cost(2.0)
    debug("Observe.simulate: set ExploredNewArea=true")
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

    -- Alarm penalty
    local alarmGuid = entity_by_name("AlarmSystem")
    if alarmGuid and get_property(alarmGuid, "Armed") == true then h = h + 20.0 end

    -- Panel unknown penalty
    local panelGuid = entity_by_name("EnergyPanel")
    if panelGuid then
        local knownSet = tag_set("Known") or {}
        local panelKnown = false
        for _, g in ipairs(knownSet) do if g == panelGuid then panelKnown = true; break end end
        if not panelKnown then h = h + 15.0 end
    end

    -- Outside penalty
    local currentRoom = get_property(agentGuid, "CurrentRoom")
    if not currentRoom or currentRoom == NullGuid then h = h + 10.0 end

    -- Missing code pieces
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
