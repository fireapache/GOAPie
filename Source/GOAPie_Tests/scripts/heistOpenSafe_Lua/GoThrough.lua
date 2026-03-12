-- GoThrough.lua
-- Navigate through portal entities (doors/windows) to reach a Known waypoint.
-- Replaces EnterThrough + MoveInside. Supports exterior-to-interior and interior-to-interior.
-- Dijkstra through traversable portals to find best reachable destination.

local NullGuid = 0

local function isInsideHouse(pos)
    return pos[1] >= -30 and pos[1] <= 30 and pos[2] >= -20 and pos[2] <= 20
end

-- Check if a portal is traversable: Known, not Locked, not AlarmGated (or alarm disabled)
local function isPortalTraversable(portalGuid, isKnown, alarmArmed)
    if not isKnown[portalGuid] then return false end
    if get_property(portalGuid, "Locked") == true then return false end
    if get_property(portalGuid, "AlarmGated") == true and alarmArmed then return false end
    return true
end

-- Get the other side of a portal relative to a waypoint
local function otherSide(portalGuid, wpGuid)
    local sideA = get_property(portalGuid, "SideA")
    local sideB = get_property(portalGuid, "SideB")
    if sideA == wpGuid then return sideB end
    if sideB == wpGuid then return sideA end
    return nil
end

-- Find nearest Known waypoint that has at least one traversable portal.
-- Returns (wpGuid, walkDist) or (nil, 0).
local function nearestPortalConnectedWP(agentLoc, waypoints, isKnown, portals, alarmArmed)
    -- Collect WPs that are a side of at least one traversable portal
    local connectedWPs = {}
    for _, portalGuid in ipairs(portals) do
        if isPortalTraversable(portalGuid, isKnown, alarmArmed) then
            local sA = get_property(portalGuid, "SideA")
            local sB = get_property(portalGuid, "SideB")
            if sA and isKnown[sA] then connectedWPs[sA] = true end
            if sB and isKnown[sB] then connectedWPs[sB] = true end
        end
    end

    local bestWp, bestDist = nil, math.huge
    for _, wpGuid in ipairs(waypoints) do
        if connectedWPs[wpGuid] then
            local wpLoc = get_property(wpGuid, "Location")
            if wpLoc then
                local dx = agentLoc[1] - wpLoc[1]
                local dy = agentLoc[2] - wpLoc[2]
                local d = math.sqrt(dx*dx + dy*dy)
                if d < bestDist then bestDist = d; bestWp = wpGuid end
            end
        end
    end
    return bestWp, (bestWp and bestDist or 0)
end

function evaluate(params)
    local agentGuid = params.agent.guid
    local agentLoc = get_property(agentGuid, "Location")
    if not agentLoc then return false end

    local waypoints = tag_set("Waypoint") or {}
    local knownSet = tag_set("Known") or {}
    local isKnown = {}
    for _, g in ipairs(knownSet) do isKnown[g] = true end

    local alarmGuid = entity_by_name("AlarmSystem")
    local alarmArmed = alarmGuid and get_property(alarmGuid, "Armed") == true
    local portals = tag_set("Portal") or {}

    -- Find nearest portal-connected waypoint
    local startWp = nearestPortalConnectedWP(agentLoc, waypoints, isKnown, portals, alarmArmed)
    if not startWp then
        debug("GoThrough.evaluate: no portal-connected waypoint -> FALSE")
        return false
    end

    -- Check if any traversable portal from startWp leads to a Known destination
    for _, portalGuid in ipairs(portals) do
        if isPortalTraversable(portalGuid, isKnown, alarmArmed) then
            local dest = otherSide(portalGuid, startWp)
            if dest and isKnown[dest] then
                debug("GoThrough.evaluate: traversable portal found -> TRUE")
                return true
            end
        end
    end

    debug("GoThrough.evaluate: no traversable portals -> FALSE")
    return false
end

function simulate(params)
    local agentGuid = params.agent.guid
    local agentLoc = get_property(agentGuid, "Location")
    if not agentLoc then return false end

    local waypoints = tag_set("Waypoint") or {}
    local knownSet = tag_set("Known") or {}
    local isKnown = {}
    for _, g in ipairs(knownSet) do isKnown[g] = true end

    local alarmGuid = entity_by_name("AlarmSystem")
    local alarmArmed = alarmGuid and get_property(alarmGuid, "Armed") == true
    local portals = tag_set("Portal") or {}

    -- Find nearest portal-connected waypoint
    local startWp, walkDist = nearestPortalConnectedWP(agentLoc, waypoints, isKnown, portals, alarmArmed)
    if not startWp then return false end

    -- Remember current room for exterior-to-interior detection
    local roomBefore = get_property(agentGuid, "CurrentRoom") or NullGuid

    -- Dijkstra through traversable portals (ensures shortest paths)
    local finalized = {}
    local frontier = { { wp = startWp, dist = 0 } }
    local reachable = {}  -- wpGuid -> totalDist

    while #frontier > 0 do
        -- Find minimum-distance node in frontier (Dijkstra extract-min)
        local minIdx = 1
        for i = 2, #frontier do
            if frontier[i].dist < frontier[minIdx].dist then minIdx = i end
        end
        local current = table.remove(frontier, minIdx)
        if not finalized[current.wp] then
            finalized[current.wp] = true
            if current.wp ~= startWp then
                reachable[current.wp] = current.dist
            end
            for _, portalGuid in ipairs(portals) do
                if isPortalTraversable(portalGuid, isKnown, alarmArmed) then
                    local dest = otherSide(portalGuid, current.wp)
                    if dest and isKnown[dest] and not finalized[dest] then
                        local currentLoc = get_property(current.wp, "Location")
                        local destLoc = get_property(dest, "Location")
                        local segDist = 0
                        if currentLoc and destLoc then
                            local dx = currentLoc[1] - destLoc[1]
                            local dy = currentLoc[2] - destLoc[2]
                            segDist = math.sqrt(dx*dx + dy*dy)
                        end
                        table.insert(frontier, { wp = dest, dist = current.dist + segDist })
                    end
                end
            end
        end
    end

    -- Score each destination: prefer unrevealed reveals, then safe room
    local bestWp, bestScore = nil, math.huge
    local safeGuid = entity_by_name("Safe")
    local safeRoom = safeGuid and get_property(safeGuid, "InRoom") or NullGuid

    for wpGuid, dist in pairs(reachable) do
        local score = dist

        -- Bonus for unrevealed reveals
        local reveals = get_property(wpGuid, "Reveals") or {}
        for _, rg in ipairs(reveals) do
            if not isKnown[rg] then score = score - 50.0; break end
        end

        -- Bonus for safe room waypoint
        if safeRoom ~= NullGuid then
            local wpLoc = get_property(wpGuid, "Location")
            local roomLoc = get_property(safeRoom, "Location")
            if wpLoc and roomLoc then
                local dx = wpLoc[1] - roomLoc[1]
                local dy = wpLoc[2] - roomLoc[2]
                if math.sqrt(dx*dx + dy*dy) < 5.0 then
                    score = score - 20.0
                end
            end
        end

        if score < bestScore then bestScore = score; bestWp = wpGuid end
    end

    if not bestWp then return false end

    local totalDist = reachable[bestWp]

    -- Move agent to destination
    local destLoc = get_property(bestWp, "Location")
    if destLoc then
        set_property(agentGuid, "Location", destLoc)
    end

    -- Update CurrentRoom based on destination
    if destLoc and isInsideHouse(destLoc) then
        local rooms = tag_set("Room") or {}
        local wholeHouseGuid = entity_by_name("WholeHouse")
        local bestRoom, bestRoomDist = nil, math.huge
        for _, roomGuid in ipairs(rooms) do
            if roomGuid ~= wholeHouseGuid then
                local roomLoc = get_property(roomGuid, "Location")
                if roomLoc then
                    local dx = destLoc[1] - roomLoc[1]
                    local dy = destLoc[2] - roomLoc[2]
                    local d = math.sqrt(dx*dx + dy*dy)
                    if d < bestRoomDist then bestRoomDist = d; bestRoom = roomGuid end
                end
            end
        end
        if bestRoom then
            set_property(agentGuid, "CurrentRoom", bestRoom)
        end
    end

    -- Include walk distance to portal-connected waypoint in cost
    set_cost((totalDist or 0) + walkDist + 1.0)

    -- Entering the mansion from outside counts as exploration
    local roomAfter = get_property(agentGuid, "CurrentRoom") or NullGuid
    if roomBefore == NullGuid and roomAfter ~= NullGuid then
        set_property(agentGuid, "ExploredNewArea", true)
    end

    debug("GoThrough.simulate: moved through portals, dist=" .. tostring(totalDist))
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
