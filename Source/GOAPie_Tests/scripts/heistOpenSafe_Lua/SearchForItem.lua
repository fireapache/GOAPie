-- SearchForItem.lua
-- Search for and pick up the nearest reachable item not yet in inventory.
-- When outside, only outside items are reachable. When inside, all items are reachable.
-- Skips non-carryable POIs (AlarmPanel, FuseBoxEntity).

local NullGuid = 0

local function isInsideHouse(pos)
    return pos[1] >= -30 and pos[1] <= 30 and pos[2] >= -20 and pos[2] <= 20
end

function evaluate(params)
    local agentGuid = params.agent.guid
    local inv = get_property(agentGuid, "Inventory") or {}
    local currentRoom = get_property(agentGuid, "CurrentRoom")
    local agentInside = currentRoom and currentRoom ~= NullGuid
    local items = tag_set("Item") or {}

    -- Build set of inventory guids for fast lookup
    local inInv = {}
    for _, g in ipairs(inv) do inInv[g] = true end

    for _, itemGuid in ipairs(items) do
        if not inInv[itemGuid] then
            -- Skip non-carryable POIs
            local panelGuid = entity_by_name("AlarmPanel")
            local fuseGuid = entity_by_name("FuseBoxEntity")
            if itemGuid ~= panelGuid and itemGuid ~= fuseGuid then
                local loc = get_property(itemGuid, "Location")
                if loc then
                    -- Only pick reachable items based on inside/outside
                    if agentInside or not isInsideHouse(loc) then
                        debug("SearchForItem.evaluate: reachable item found -> TRUE")
                        return true
                    end
                end
            end
        end
    end

    debug("SearchForItem.evaluate: no reachable items -> FALSE")
    return false
end

function simulate(params)
    local agentGuid = params.agent.guid
    local inv = get_property(agentGuid, "Inventory") or {}
    local currentRoom = get_property(agentGuid, "CurrentRoom")
    local agentInside = currentRoom and currentRoom ~= NullGuid
    local items = tag_set("Item") or {}
    local agentLoc = get_property(agentGuid, "Location")
    local panelGuid = entity_by_name("AlarmPanel")
    local fuseGuid = entity_by_name("FuseBoxEntity")

    local inInv = {}
    for _, g in ipairs(inv) do inInv[g] = true end

    -- Find nearest reachable item
    local bestGuid, bestDist = nil, math.huge
    for _, itemGuid in ipairs(items) do
        if not inInv[itemGuid] and itemGuid ~= panelGuid and itemGuid ~= fuseGuid then
            local loc = get_property(itemGuid, "Location")
            if loc and (agentInside or not isInsideHouse(loc)) then
                if agentLoc then
                    local dx = agentLoc[1] - loc[1]
                    local dy = agentLoc[2] - loc[2]
                    local d = math.sqrt(dx*dx + dy*dy)
                    if d < bestDist then bestDist = d; bestGuid = itemGuid end
                end
            end
        end
    end

    if not bestGuid then return false end
    local dist = move_agent_to_entity(bestGuid)

    -- Add item to inventory
    table.insert(inv, bestGuid)
    set_property(agentGuid, "Inventory", inv)

    set_cost((dist or 0) + 1.0)
    debug("SearchForItem.simulate: picked up item, dist=" .. tostring(dist))
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
        local alarmGuid = entity_by_name("AlarmSystem")
        if alarmGuid and get_property(alarmGuid, "Armed") == true then h = h + 10.0 end
    end

    return h
end
