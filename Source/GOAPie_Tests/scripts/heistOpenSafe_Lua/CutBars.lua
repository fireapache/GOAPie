-- CutBars.lua
-- Cut bars from a barred connector using bolt cutters.
-- Precondition: agent has BoltCutter and a barred connector exists.

local NullGuid = 0

local function hasBoltCutter(agentGuid)
    local inv = get_property(agentGuid, "Inventory") or {}
    local bcInfo = entity_by_name("BoltCutterInfo")
    if not bcInfo then return false end
    for _, itemGuid in ipairs(inv) do
        local info = get_property(itemGuid, "Info")
        if info and info == bcInfo then return true end
    end
    return false
end

function evaluate(params)
    local agentGuid = params.agent.guid
    if not hasBoltCutter(agentGuid) then
        debug("CutBars.evaluate: no bolt cutter -> FALSE")
        return false
    end

    local accessPoints = tag_set("Access") or {}
    for _, connGuid in ipairs(accessPoints) do
        if get_property(connGuid, "Barred") == true then
            debug("CutBars.evaluate: barred connector found -> TRUE")
            return true
        end
    end

    debug("CutBars.evaluate: no barred connector -> FALSE")
    return false
end

function simulate(params)
    local agentGuid = params.agent.guid
    local accessPoints = tag_set("Access") or {}

    local bestGuid, bestDist = nil, math.huge
    local agentLoc = get_property(agentGuid, "Location")
    for _, connGuid in ipairs(accessPoints) do
        if get_property(connGuid, "Barred") == true then
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
    set_property(bestGuid, "Barred", false)
    set_cost((dist or 0) + 5.0)
    debug("CutBars.simulate: bars cut, dist=" .. tostring(dist))
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
