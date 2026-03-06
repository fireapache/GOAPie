-- MoveInside.lua
-- Navigate inside the house from current room to the safe room.
-- Precondition: agent is inside (has a CurrentRoom set).

local NullGuid = 0

function evaluate(params)
    local agentGuid = params.agent.guid
    local currentRoom = get_property(agentGuid, "CurrentRoom")
    local inside = currentRoom and currentRoom ~= NullGuid
    debug("MoveInside.evaluate: inside=" .. tostring(inside))
    return inside
end

function simulate(params)
    local agentGuid = params.agent.guid
    local currentRoom = get_property(agentGuid, "CurrentRoom")
    local safeGuid = entity_by_name("Safe")
    if not safeGuid then return false end

    local safeRoom = get_property(safeGuid, "InRoom")
    if not safeRoom or safeRoom == NullGuid then return false end
    if currentRoom == safeRoom then
        debug("MoveInside.simulate: already in safe room -> FALSE")
        return false
    end

    local dist = move_agent_to_entity(safeRoom)
    set_property(agentGuid, "CurrentRoom", safeRoom)
    local roomLoc = get_property(safeRoom, "Location")
    if roomLoc then set_property(agentGuid, "Location", roomLoc) end

    set_cost((dist or 0) + 1.0)
    debug("MoveInside.simulate: moved to safe room, dist=" .. tostring(dist))
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
