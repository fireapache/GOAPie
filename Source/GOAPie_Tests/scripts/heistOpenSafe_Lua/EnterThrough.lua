-- EnterThrough.lua
-- Cross an entry connector to get inside the house.
-- Precondition: agent outside, at least one free (unlocked, unblocked, unalarmed) connector.

local NullGuid = 0

local function isInsideHouse(pos)
    return pos[1] >= -30 and pos[1] <= 30 and pos[2] >= -20 and pos[2] <= 20
end

function evaluate(params)
    local agentGuid = params.agent.guid
    local currentRoom = get_property(agentGuid, "CurrentRoom")
    if currentRoom and currentRoom ~= NullGuid then
        debug("EnterThrough.evaluate: already inside -> FALSE")
        return false
    end

    local alarmArmed = true
    local alarmGuid = entity_by_name("AlarmSystem")
    if alarmGuid then alarmArmed = (get_property(alarmGuid, "Armed") == true) end

    local accessPoints = tag_set("Access") or {}
    for _, connGuid in ipairs(accessPoints) do
        local locked = get_property(connGuid, "Locked") == true
        local blocked = get_property(connGuid, "Blocked") == true
        local barred = get_property(connGuid, "Barred") == true
        local alarmed = get_property(connGuid, "Alarmed") == true
        if not locked and not blocked and not barred and not (alarmArmed and alarmed) then
            debug("EnterThrough.evaluate: free connector -> TRUE")
            return true
        end
    end

    debug("EnterThrough.evaluate: all entries blocked -> FALSE")
    return false
end

function simulate(params)
    local agentGuid = params.agent.guid

    local alarmArmed = true
    local alarmGuid = entity_by_name("AlarmSystem")
    if alarmGuid then alarmArmed = (get_property(alarmGuid, "Armed") == true) end

    local accessPoints = tag_set("Access") or {}
    local bestGuid, bestDist = nil, math.huge
    local agentLoc = get_property(agentGuid, "Location")

    for _, connGuid in ipairs(accessPoints) do
        local locked = get_property(connGuid, "Locked") == true
        local blocked = get_property(connGuid, "Blocked") == true
        local barred = get_property(connGuid, "Barred") == true
        local alarmed = get_property(connGuid, "Alarmed") == true
        if not locked and not blocked and not barred and not (alarmArmed and alarmed) then
            local connLoc = get_property(connGuid, "Location")
            if agentLoc and connLoc then
                local dx = agentLoc[1] - connLoc[1]
                local dy = agentLoc[2] - connLoc[2]
                local d = math.sqrt(dx*dx + dy*dy)
                if d < bestDist then bestDist = d; bestGuid = connGuid end
            end
        end
    end

    if not bestGuid then return false end
    local dist = move_agent_to_entity(bestGuid)
    set_cost((dist or 0) + 1.0)

    -- Find nearest room to the access point (skip WholeHouse)
    local accessLoc = get_property(bestGuid, "Location")
    local rooms = tag_set("Room") or {}
    local wholeHouseGuid = entity_by_name("WholeHouse")
    local bestRoom, bestRoomDist = nil, math.huge
    for _, roomGuid in ipairs(rooms) do
        if roomGuid ~= wholeHouseGuid then
            local roomLoc = get_property(roomGuid, "Location")
            if roomLoc and accessLoc then
                local dx = accessLoc[1] - roomLoc[1]
                local dy = accessLoc[2] - roomLoc[2]
                local d = math.sqrt(dx*dx + dy*dy)
                if d < bestRoomDist then
                    bestRoomDist = d
                    bestRoom = roomGuid
                end
            end
        end
    end

    if bestRoom then
        set_property(agentGuid, "CurrentRoom", bestRoom)
        local roomLoc = get_property(bestRoom, "Location")
        if roomLoc then set_property(agentGuid, "Location", roomLoc) end
    end

    debug("EnterThrough.simulate: entered, dist=" .. tostring(dist))
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

    -- Entry penalties when outside
    local currentRoom = get_property(agentGuid, "CurrentRoom")
    if not currentRoom or currentRoom == NullGuid then
        local accessPoints = tag_set("Access") or {}
        local alarmArmed = true
        local alarmGuid = entity_by_name("AlarmSystem")
        if alarmGuid then alarmArmed = (get_property(alarmGuid, "Armed") == true) end
        local extra = 0
        for _, connGuid in ipairs(accessPoints) do
            if get_property(connGuid, "Locked") == true then extra = extra + 10 end
            if get_property(connGuid, "Barred") == true then extra = extra + 12 end
            if alarmArmed and get_property(connGuid, "Alarmed") == true then extra = extra + 8 end
        end
        h = h + extra * 0.25
    end

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
        local alarmGuid2 = entity_by_name("AlarmSystem")
        if alarmGuid2 and get_property(alarmGuid2, "Armed") == true then h = h + 10.0 end
    end

    return h
end
