-- Lockpick.lua
-- Pick a lock on a locked connector using a lockpick set from inventory.
-- Precondition: agent has LockpickSet and a locked connector exists.

local NullGuid = 0

local function hasLockpick(agentGuid)
    local inv = get_property(agentGuid, "Inventory") or {}
    local lpInfo = entity_by_name("LockpickInfo")
    if not lpInfo then return false end
    for _, itemGuid in ipairs(inv) do
        local info = get_property(itemGuid, "Info")
        if info and info == lpInfo then return true end
    end
    return false
end

function evaluate(params)
    local agentGuid = params.agent.guid
    if not hasLockpick(agentGuid) then
        debug("Lockpick.evaluate: no lockpick -> FALSE")
        return false
    end

    local accessPoints = tag_set("Access") or {}
    for _, connGuid in ipairs(accessPoints) do
        if get_property(connGuid, "Locked") == true then
            debug("Lockpick.evaluate: locked connector found -> TRUE")
            return true
        end
    end

    debug("Lockpick.evaluate: no locked connector -> FALSE")
    return false
end

function simulate(params)
    local agentGuid = params.agent.guid
    local accessPoints = tag_set("Access") or {}

    local bestGuid, bestDist = nil, math.huge
    local agentLoc = get_property(agentGuid, "Location")
    for _, connGuid in ipairs(accessPoints) do
        if get_property(connGuid, "Locked") == true then
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
    set_property(bestGuid, "Locked", false)
    set_cost((dist or 0) + 4.0)
    debug("Lockpick.simulate: picked lock, dist=" .. tostring(dist))
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
