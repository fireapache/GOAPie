-- BreakConnector.lua
-- Break open a locked/blocked/barred connector using a crowbar.
-- Precondition: agent has Crowbar and a blocked connector exists.

local NullGuid = 0

local function hasCrowbar(agentGuid)
    local inv = get_property(agentGuid, "Inventory") or {}
    local crInfo = entity_by_name("CrowbarInfo")
    if not crInfo then return false end
    for _, itemGuid in ipairs(inv) do
        local info = get_property(itemGuid, "Info")
        if info and info == crInfo then return true end
    end
    return false
end

local function isBlocked(connGuid)
    if get_property(connGuid, "Locked") == true then return true end
    if get_property(connGuid, "Blocked") == true then return true end
    if get_property(connGuid, "Barred") == true then return true end
    return false
end

function evaluate(params)
    local agentGuid = params.agent.guid
    if not hasCrowbar(agentGuid) then
        debug("BreakConnector.evaluate: no crowbar -> FALSE")
        return false
    end

    local accessPoints = tag_set("Access") or {}
    for _, connGuid in ipairs(accessPoints) do
        if isBlocked(connGuid) then
            debug("BreakConnector.evaluate: blocked connector found -> TRUE")
            return true
        end
    end

    debug("BreakConnector.evaluate: nothing to break -> FALSE")
    return false
end

function simulate(params)
    local agentGuid = params.agent.guid
    local accessPoints = tag_set("Access") or {}

    local bestGuid, bestDist = nil, math.huge
    local agentLoc = get_property(agentGuid, "Location")
    for _, connGuid in ipairs(accessPoints) do
        if isBlocked(connGuid) then
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
    set_property(bestGuid, "Blocked", false)
    set_property(bestGuid, "Barred", false)
    set_cost((dist or 0) + 6.0)
    debug("BreakConnector.simulate: connector broken, dist=" .. tostring(dist))
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
    return math.sqrt(dx*dx + dy*dy)
end
